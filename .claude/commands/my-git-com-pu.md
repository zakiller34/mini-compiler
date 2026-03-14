---
description: "Stage, commit, and push to main"
allowed-tools: [Bash(git status:*), Bash(git add:*), Bash(git diff:*), Bash(git log:*), Bash(git commit:*), Bash(git push:*)]
---

## Context

- Current branch: !`git branch --show-current`
- Git status: !`git status --short`
- Staged diff: !`git diff --cached --stat`
- Unstaged diff: !`git diff --stat`
- Recent commits: !`git log --oneline -5 2>/dev/null || echo "(no commits)"`

## Instructions

1. **Check branch**: Confirm we're on `main`. If not, warn and stop.

2. **Stage**: Run `git status` to see changes. Stage all relevant files by name (not `git add .`). Never stage `.env`, credentials, or secrets.

3. **Diff review**: Run `git diff --cached` to see what will be committed. Summarize changes.

4. **Commit message**: Draft a concise commit message based on the diff:
   - Summarize the "why" not the "what"
   - Follow the repo's existing commit style from recent commits
   - Use a HEREDOC to pass the message
   - Do NOT add Co-Authored-By lines

5. **Commit**: Create the commit.

6. **Push**: Run `git push origin main`.

7. **Verify**: Run `git status` + `git log --oneline -3` to confirm success.

If user provides arguments, use them as the commit message: $ARGUMENTS
