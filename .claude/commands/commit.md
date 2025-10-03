Create a git commit following the project conventions

## IMPORTANT: No Claude Attribution

**NEVER add Claude Code attribution or Co-Authored-By lines to commits.**

Reason: PRs use squash-and-merge, which concatenates all commit messages. Multiple Claude attributions would be duplicated in the final commit, creating noise.

## Steps

1. Format, build, test: `find src include test -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i && bazel build //... && bazel test //...`
2. Check status and style: `git status`, `git diff`, `git log --oneline -5`, `git show HEAD --stat`
3. If on main branch, stop and ask user to switch to feature branch
4. Stage files with `git add` command
5. Create commit with separate `git commit` command (do NOT concatenate add and commit)
6. Commit message format:
   - **Simple changes** (1 file, doc updates): 1-2 bullets
   - **Medium changes** (few files, features): 2-3 bullets
   - **Major changes** (refactors, systems): 3-5 bullets
   - Short summary line, bullet points using `-`
   - Focus on what changed, not process or attribution
