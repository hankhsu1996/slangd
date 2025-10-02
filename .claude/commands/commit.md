Create a git commit following the project conventions

## Steps

1. Format, build, test: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i && bazel build //... && bazel test //...`
2. Check status and style: `git status`, `git diff`, `git log --oneline -5`, `git show HEAD --stat`
3. If on main branch, stop and ask user to switch to feature branch
4. Create commit matching scope:
   - **Simple changes** (1 file, doc updates): 1-2 bullets
   - **Medium changes** (few files, features): 2-3 bullets
   - **Major changes** (refactors, systems): 3-5 bullets
   - Short summary line, bullet points using `-`
   - No Claude attribution, focus on what changed
