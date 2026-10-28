#include <tickforge/adapters/binance/capture.hpp>
#include <tickforge/adapters/binance/binance_protocol.hpp>
#include <tickforge/core/book_tracker.hpp>
#include <tickforge/console/console.hpp>
#include <tickforge/core/recorder.hpp>

#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <openssl/ssl.h>
#include <chrono>
#include <algorithm>
#include <csignal>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace tickforge::adapters::binance {
namespace {
namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = net::ip::tcp;
using Error = boost::system::error_code;
using Clock = std::chrono::steady_clock;
constexpr auto rest_host = "data-api.binance.vision";
constexpr auto stream_host = "data-stream.binance.vision";
// One combined connection keeps depth and trades in a single receipt order.
constexpr auto stream_target = "/stream?streams=btcusdt@depth@100ms/btcusdt@trade";

void configure_tls(ssl::stream<beast::tcp_stream>& stream, const char* host)
{
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host))
        throw std::runtime_error("TLS SNI configuration failed");
    stream.set_verify_mode(ssl::verify_peer);
    stream.set_verify_callback(ssl::host_name_verification(host));
}

class HttpsGet : public std::enable_shared_from_this<HttpsGet> {
public:
    using Done = std::function<void(Error, unsigned, std::string, std::string)>;
    HttpsGet(net::io_context& io, ssl::context& tls, std::string target, Done done)
        : resolver_(io), stream_(io, tls), done_(std::move(done))
    {
        configure_tls(stream_, rest_host);
        request_ = {http::verb::get, std::move(target), 11};
        request_.set(http::field::host, rest_host);
        request_.set(http::field::user_agent, "tickforge/0.1 public-market-research");
        response_.body_limit(4 * 1024 * 1024);
    }
    void start()
    {
        resolver_.async_resolve(rest_host, "443", [self = shared_from_this()](Error ec, tcp::resolver::results_type endpoints) {
            if (ec) return self->finish(ec);
            beast::get_lowest_layer(self->stream_).expires_after(std::chrono::seconds(10));
            beast::get_lowest_layer(self->stream_).async_connect(endpoints,
                [self](Error error, const tcp::endpoint&) {
                    if (error) return self->finish(error);
                    self->stream_.async_handshake(ssl::stream_base::client, [self](Error error) {
                        if (error) return self->finish(error);
                        http::async_write(self->stream_, self->request_, [self](Error error, std::size_t) {
                            if (error) return self->finish(error);
                            http::async_read(self->stream_, self->buffer_, self->response_,
                                [self](Error error, std::size_t) { self->finish(error); });
                        });
                    });
                });
        });
    }
    void stop()
    {
        resolver_.cancel();
        Error ignored;
        beast::get_lowest_layer(stream_).socket().close(ignored);
    }
private:
    void finish(Error ec)
    {
        auto done = std::move(done_);
        stop();
        if (done) {
            auto& response = response_.get();
            done(ec, response.result_int(), std::move(response.body()),
                std::string(response[http::field::retry_after]));
        }
    }
    tcp::resolver resolver_;
    ssl::stream<beast::tcp_stream> stream_;
    beast::flat_buffer buffer_;
    http::request<http::empty_body> request_;
    http::response_parser<http::string_body> response_;
    Done done_;
};

class StreamSocket : public std::enable_shared_from_this<StreamSocket> {
public:
    using Message = std::function<void(std::string)>;
    StreamSocket(net::io_context& io, ssl::context& tls, Message message, std::function<void(Error)> done)
        : resolver_(io), stream_(io, tls), message_(std::move(message)), done_(std::move(done))
    {
        configure_tls(stream_.next_layer(), stream_host);
        stream_.read_message_max(2 * 1024 * 1024);
    }
    void start()
    {
        resolver_.async_resolve(stream_host, "443", [self = shared_from_this()](Error ec, tcp::resolver::results_type endpoints) {
            if (ec) return self->finish(ec);
            beast::get_lowest_layer(self->stream_).expires_after(std::chrono::seconds(10));
            beast::get_lowest_layer(self->stream_).async_connect(endpoints, [self](Error error, const tcp::endpoint&) {
                if (error) return self->finish(error);
                self->stream_.next_layer().async_handshake(ssl::stream_base::client, [self](Error error) {
                    if (error) return self->finish(error);
                    beast::get_lowest_layer(self->stream_).expires_never();
                    self->stream_.set_option(websocket::stream_base::timeout{
                        std::chrono::seconds(10), std::chrono::seconds(10), true});
                    self->stream_.async_handshake(stream_host, stream_target, [self](Error error) {
                        if (error) return self->finish(error);
                        self->read();
                    });
                });
            });
        });
    }
    void stop()
    {
        resolver_.cancel();
        Error ignored;
        beast::get_lowest_layer(stream_).socket().close(ignored);
    }
private:
    void read()
    {
        stream_.async_read(buffer_, [self = shared_from_this()](Error ec, std::size_t) {
            if (ec) return self->finish(ec);
            auto message = beast::buffers_to_string(self->buffer_.data());
            self->buffer_.consume(self->buffer_.size());
            if (self->message_) self->message_(std::move(message));
            if (self->stream_.is_open()) self->read();
        });
    }
    void finish(Error ec)
    {
        auto done = std::move(done_);
        message_ = {};
        stop();
        if (done) done(ec);
    }
    tcp::resolver resolver_;
    websocket::stream<ssl::stream<beast::tcp_stream>> stream_;
    beast::flat_buffer buffer_;
    Message message_;
    std::function<void(Error)> done_;
};

class Capture : public std::enable_shared_from_this<Capture> {
public:
    Capture(net::io_context& io, const std::string& path, unsigned seconds)
        : io_(io), tls_(ssl::context::tls_client), recorder_(path), deadline_(io), watchdog_(io), retry_(io),
          signals_(io, SIGINT, SIGTERM), seconds_(seconds)
    {
        tls_.set_default_verify_paths();
        tls_.set_options(ssl::context::no_sslv2 | ssl::context::no_sslv3 | ssl::context::no_tlsv1 | ssl::context::no_tlsv1_1);
    }
    void start()
    {
        signals_.async_wait([self = shared_from_this()](Error ec, int) {
            if (!ec) self->guard([&] { self->complete(); });
        });
        deadline_.expires_after(std::chrono::seconds(seconds_));
        deadline_.async_wait([self = shared_from_this()](Error ec) {
            if (!ec) self->guard([&] { self->complete(); });
        });
        write_console(std::format(
            "tickforge | Public Binance depth and trade capture\n"
            "REST      | https://{}\nWebSocket | wss://{}{}\n"
            "Mode      | SIMULATION ONLY | BTCUSDT | no keys or orders\n"
            "Duration  | {} seconds including connection/synchronization\n", rest_host, stream_host, stream_target, seconds_));
        request("/api/v3/exchangeInfo?symbol=BTCUSDT", [self = shared_from_this()](std::string body, EventTime) {
            self->rules_ = parse_instrument(body);
            write_console(std::format("Metadata  | tick={} USDT | step={} BTC\n",
                price_text(self->rules_.price_tick_atoms), quantity_text(self->rules_.quantity_step_atoms)));
            self->connect();
        });
    }
    const std::string& failure() const { return failure_; }
private:
    template<class Function> void guard(Function function)
    {
        if (stopping_) return;
        try { function(); }
        catch (const std::exception& error) { fail(error.what()); }
    }
    EventTime receive_time()
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - origin_).count();
        const auto received = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        return {0, 0, received, elapsed};
    }
    template<class Data> BookResult emit(Data data, std::int64_t exchange = 0, std::optional<EventTime> received = std::nullopt)
    {
        if (ordinal_ == std::numeric_limits<std::int64_t>::max()) throw std::runtime_error("event ordinal overflow");
        auto time = received ? *received : receive_time();
        time.ordinal = ++ordinal_;
        time.exchange_ns = exchange;
        MarketEvent event{time, std::move(data)};
        recorder_.append(event); // Record first, so a storage failure stops processing.
        const auto result = std::visit([&](const auto& value) -> BookResult {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, Trade>) return BookResult::stale;
            else return tracker_.apply(value);
        }, event.data);
        if (tracker_.view().synchronized()) ever_ready_ = true;
        const auto now = Clock::now();
        if ((result != BookResult::applied && result != BookResult::stale &&
            result != BookResult::awaiting_snapshot && result != BookResult::buffering) || now - last_display_ >= std::chrono::seconds(1)) {
            write_console(std::format("\nEvent #{} | elapsed={} ns | {}\n{}",
                ordinal_, event.time.receive_elapsed_ns, result_text(result), book_text(tracker_.view())));
            if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
            last_display_ = now;
        }
        return result;
    }
    void request(std::string target, std::function<void(std::string, EventTime)> done)
    {
        const auto generation = generation_;
        request_ = std::make_shared<HttpsGet>(io_, tls_, std::move(target),
            [self = shared_from_this(), generation, done = std::move(done)](Error ec, unsigned status, std::string body, std::string retry_after) {
                if (generation != self->generation_) return;
                const auto received = self->receive_time();
                self->guard([&] {
                    self->request_.reset();
                    if (ec) return self->reconnect(std::format("REST transport: {}", ec.message()));
                    if (status != 200) {
                        // Rate-limit/regional/permission errors are never retried blindly.
                        throw std::runtime_error(std::format("REST HTTP {}; Retry-After='{}'; {}",
                            status, retry_after, body.substr(0, 300)));
                    }
                    done(std::move(body), received);
                });
            });
        request_->start();
    }
    void connect()
    {
        emit(BookReset{"connecting"});
        last_depth_ = Clock::now();
        const auto generation = generation_;
        socket_ = std::make_shared<StreamSocket>(io_, tls_,
            [self = shared_from_this(), generation](std::string message) {
                if (generation != self->generation_) return;
                const auto received = self->receive_time();
                self->guard([&] {
                    if (is_server_shutdown(message)) return self->reconnect("server_shutdown");
                    auto parsed = parse_stream_message(message, self->rules_);
                    if (auto* trade = std::get_if<Trade>(&parsed.data)) {
                        // Trades never refresh depth staleness: a quiet market is not a broken feed,
                        // and a live trade stream does not prove that depth updates still arrive.
                        ++self->trades_;
                        self->emit(*trade, parsed.exchange_ns, received);
                        return;
                    }
                    self->last_depth_ = Clock::now();
                    self->emit(std::get<BookUpdate>(std::move(parsed.data)), parsed.exchange_ns, received);
                    if (self->tracker_.needs_snapshot() && !self->request_) self->snapshot();
                });
            }, [self = shared_from_this(), generation](Error ec) {
                if (generation != self->generation_) return;
                self->guard([&] { self->reconnect(std::format("WebSocket: {}", ec.message())); });
            });
        socket_->start();
        watch();
    }
    void snapshot()
    {
        if (++snapshot_requests_ > 12) throw std::runtime_error("snapshot retry budget exhausted");
        write_console("Snapshot  | requesting up to 5000 levels per side; stream continues buffering\n");
        request("/api/v3/depth?symbol=BTCUSDT&limit=5000", [self = shared_from_this()](std::string body, EventTime received) {
            const auto result = self->emit(parse_snapshot(body, self->rules_), 0, received);
            if (result == BookResult::invalid) throw std::runtime_error("invalid REST snapshot/book");
            // A gap during bootstrap waits for the next depth message before retrying.
        });
    }
    void watch()
    {
        watchdog_.expires_after(std::chrono::seconds(1));
        const auto generation = generation_;
        watchdog_.async_wait([self = shared_from_this(), generation](Error ec) {
            if (ec || generation != self->generation_) return;
            self->guard([&] {
                if (Clock::now() - self->last_depth_ > std::chrono::seconds(5)) self->reconnect("stale_depth");
                else self->watch();
            });
        });
    }
    void cancel_network()
    {
        ++generation_;
        watchdog_.cancel();
        if (request_) { request_->stop(); request_.reset(); }
        if (socket_) { socket_->stop(); socket_.reset(); }
    }
    void reconnect(const std::string& reason)
    {
        cancel_network();
        emit(BookReset{reason == "stale_depth" ? "stale_depth" : "disconnect"});
        if (++reconnects_ > 5) throw std::runtime_error("reconnect retry budget exhausted");
        const auto delay = std::min(1U << (reconnects_ - 1), 8U);
        write_console(std::format("Reconnect | {} | retry {}/5 in {} seconds\n", reason, reconnects_, delay));
        retry_.expires_after(std::chrono::seconds(delay));
        retry_.async_wait([self = shared_from_this()](Error ec) {
            if (!ec) self->guard([&] {
                if (self->rules_.price_tick_atoms == 0) self->start_metadata_retry();
                else self->connect();
            });
        });
    }
    void start_metadata_retry()
    {
        request("/api/v3/exchangeInfo?symbol=BTCUSDT", [self = shared_from_this()](std::string body, EventTime) {
            self->rules_ = parse_instrument(body);
            self->connect();
        });
    }
    void complete()
    {
        if (!ever_ready_) throw std::runtime_error("capture ended before any synchronized book was established");
        cancel_network();
        deadline_.cancel();
        retry_.cancel();
        signals_.cancel();
        emit(BookReset{"shutdown"});
        recorder_.finish();
        stopping_ = true;
        write_console(std::format("\nCapture complete | {} events ({} trades) | {} reconnects | recording footer saved\n",
            ordinal_, trades_, reconnects_));
    }
    void fail(std::string message)
    {
        failure_ = std::move(message);
        stopping_ = true;
        cancel_network();
        deadline_.cancel();
        retry_.cancel();
        signals_.cancel();
        // No footer: replay will explicitly reject this incomplete recording.
    }
    net::io_context& io_;
    ssl::context tls_;
    SessionRecorder recorder_;
    net::steady_timer deadline_, watchdog_, retry_;
    net::signal_set signals_;
    unsigned seconds_;
    InstrumentRules rules_{};
    BookTracker tracker_;
    std::shared_ptr<HttpsGet> request_;
    std::shared_ptr<StreamSocket> socket_;
    Clock::time_point origin_ = Clock::now(), last_depth_ = origin_, last_display_ = origin_;
    std::int64_t ordinal_ = 0, trades_ = 0;
    unsigned generation_ = 0, reconnects_ = 0, snapshot_requests_ = 0;
    bool stopping_ = false, ever_ready_ = false;
    std::string failure_;
};
} // namespace

void capture(const std::string& path, unsigned seconds)
{
    if (seconds == 0 || seconds > 3600) throw std::runtime_error("capture duration must be 1..3600 seconds");
    net::io_context io;
    auto capture = std::make_shared<Capture>(io, path, seconds);
    capture->start();
    io.run();
    if (!capture->failure().empty()) throw std::runtime_error(capture->failure());
    if (std::fflush(stdout) != 0) throw std::runtime_error("console flush failed");
}
} // namespace tickforge::adapters::binance
