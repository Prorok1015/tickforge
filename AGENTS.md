# Working on tickforge

- Keep code, comments, and project documentation in English. Explain work to the user in Russian.
- Keep Linux as the target platform and state separately what was checked in WSL and native Windows.
- Organize each production CMake target in its own directory with its own `CMakeLists.txt`.
  Put reusable headers under that target's `include/tickforge/<component>/` and implementations
  under `src/`. Declare dependencies on the owning target, with appropriate PUBLIC/PRIVATE scope.
- Keep `main.cpp` as a small entry point. Argument handling, commands, presentation, domain
  logic, and network adapters belong to their respective components.
- Finish work in small stages. After each completed stage, run the relevant checks and create
  a local Git commit with an accurate English message. Do not push without the user's request.
  Preserve unrelated user changes and inspect staged files before committing.
- Keep documentation consistent with implemented behavior. Update `docs/code-map.md` when
  adding, removing, or changing component boundaries.
- Real order submission and trading keys are outside the current scope. Keep actual market
  recordings private under the ignored `recordings/` directory; use synthetic fixtures in Git.
