Create a GitHub pull request following project conventions

## Steps

1. Push branch: `git push -u origin HEAD` (handles both new and existing branches)
2. Review commits: `git log origin/main..HEAD --format='%s%n%b%n---'`
3. Check changes overview: `git diff origin/main...HEAD --stat`
4. Create PR with structure:
   - **Summary**: What this PR does (user-facing benefit)
   - **Key Changes**: Group by major features (give each proportional space based on scope)
   - **Documentation**: New/updated docs
   - **Testing**: What was tested
5. Important principles:
   - Review commit messages to understand full scope - don't rely on memory
   - Organize by themes, not commit chronology - group related work into logical sections
   - Major features deserve detailed sections with subsections
   - Emphasize breakthroughs and difficult problems solved
   - Avoid redundant sections
   - Focus on what changed and why, not internal process or philosophy

## Example Structure

For a PR with 15 commits spanning architecture refactor + new feature:

- Give each major work area a detailed section
- Use subsections to break down complex changes
- Highlight achievements explicitly (e.g., "Breakthrough Feature")
- Don't bury significant work under single bullets
