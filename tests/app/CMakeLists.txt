add_test(NAME cli_smoke COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:tickforge>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_smoke.cmake)
add_test(NAME replay_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:tickforge>
    -DFIXTURE=${PROJECT_SOURCE_DIR}/examples/synthetic.csv
    -P ${CMAKE_CURRENT_SOURCE_DIR}/replay_cli.cmake)
add_test(NAME book_replay_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:tickforge>
    -DFIXTURE=${PROJECT_SOURCE_DIR}/examples/synthetic-book.csv
    -P ${CMAKE_CURRENT_SOURCE_DIR}/book_replay_cli.cmake)
add_test(NAME paper_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:tickforge>
    -DSESSION=${PROJECT_SOURCE_DIR}/examples/synthetic-book.csv
    -DSCENARIO=${PROJECT_SOURCE_DIR}/examples/paper-orders.csv
    -P ${CMAKE_CURRENT_SOURCE_DIR}/paper_cli.cmake)
add_test(NAME strategy_cli COMMAND ${CMAKE_COMMAND}
    -DPROGRAM=$<TARGET_FILE:tickforge>
    -DSESSION=${PROJECT_SOURCE_DIR}/examples/threshold-market.csv
    -DSCENARIO=${PROJECT_SOURCE_DIR}/examples/threshold-config.csv
    -P ${CMAKE_CURRENT_SOURCE_DIR}/strategy_cli.cmake)
