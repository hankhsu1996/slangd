Create a git commit following the project conventions

## Steps

1. Format code: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i`
2. Build: `bazel build //...`
3. Test: `bazel test //...`
4. Check git status and diff. If we are on the main branch, stop and ask the user if they want to switch to a feature branch.
5. Create commit following this format:
   - Short summary + bullet points using `-`
   - No Claude attribution
   - Focus on what changed
6. Commit messages: Short summary + 2–5 bullet points using `-` in details, no Claude attribution, focus on what changed, not process or steps
