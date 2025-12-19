#!/usr/bin/env python3
"""
repoWatch Git Module

Git repository status tracking and commit monitoring.
"""

import os
import subprocess
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional, NamedTuple, Any
from dataclasses import dataclass
import json
import sys

try:
    import git
    from git import Repo
    GITPYTHON_AVAILABLE = True
except ImportError:
    GITPYTHON_AVAILABLE = False


@dataclass
class GitFileStatus:
    """Represents the status of a file in git."""
    path: str
    status: str  # 'modified', 'added', 'deleted', 'untracked', etc.
    staged: bool = False

    @property
    def status_symbol(self) -> str:
        """Get a symbol representing the file status."""
        symbols = {
            'modified': 'M',
            'added': 'A',
            'deleted': 'D',
            'untracked': '?',
            'renamed': 'R',
        }
        return symbols.get(self.status, self.status[0].upper())


@dataclass
class GitCommit:
    """Represents a git commit."""
    sha: str
    message: str
    author: str
    timestamp: datetime
    files: List[str]


class GitTracker:
    """Tracks git repository status and commits."""

    def __init__(self, repo_path: Path, config: Optional[Dict[str, Any]] = None):
        self.repo_path = repo_path.resolve()
        self.config = config or {}
        self.git_config = self.config.get("config", {}).get("git_settings", {})
        self.fallback_config = self.config.get("config", {}).get("fallback_mode", {})

        # Initialize git repo
        self.repo: Optional[Repo] = None
        self.use_subprocess = not GITPYTHON_AVAILABLE or self.fallback_config.get("use_subprocess", False)

        if not self.use_subprocess and GITPYTHON_AVAILABLE:
            try:
                self.repo = Repo(self.repo_path)
            except Exception as e:
                print(f"Warning: Failed to initialize GitPython repo: {e}")
                self.use_subprocess = True

    def _run_git_command(self, args: List[str], cwd: Optional[Path] = None) -> Tuple[str, str, int]:
        """Run a git command and return stdout, stderr, returncode."""
        cmd = [self.config.get("paths", {}).get("git_binary", "git")] + args
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd or self.repo_path,
                capture_output=True,
                text=True,
                timeout=30
            )
            return result.stdout, result.stderr, result.returncode
        except subprocess.TimeoutExpired:
            return "", "Command timed out", -1
        except FileNotFoundError:
            return "", "Git command not found", -1

    def get_branch_name(self) -> str:
        """Get the current branch name."""
        if not self.use_subprocess and self.repo:
            try:
                return self.repo.active_branch.name
            except Exception:
                pass

        # Fallback to subprocess
        stdout, stderr, returncode = self._run_git_command(["branch", "--show-current"])
        if returncode == 0:
            return stdout.strip()
        return "unknown"

    def get_status(self) -> List[GitFileStatus]:
        """Get the current git status."""
        if not self.use_subprocess and self.repo:
            try:
                return self._get_status_gitpython()
            except Exception:
                pass

        # Fallback to subprocess
        return self._get_status_subprocess()

    def _get_status_gitpython(self) -> List[GitFileStatus]:
        """Get status using GitPython."""
        if not self.repo:
            return []

        status = []
        git_status = self.repo.git.status(porcelain=True)

        for line in git_status.split('\n'):
            if not line.strip():
                continue

            # Parse porcelain format: XY file
            staged_status = line[0]
            unstaged_status = line[1]
            file_path = line[3:]

            # Determine overall status
            if unstaged_status != ' ':
                git_status_str = self._porcelain_to_status(unstaged_status)
                staged = False
            elif staged_status != ' ':
                git_status_str = self._porcelain_to_status(staged_status)
                staged = True
            else:
                continue

            status.append(GitFileStatus(
                path=file_path,
                status=git_status_str,
                staged=staged
            ))

        return status

    def _get_status_subprocess(self) -> List[GitFileStatus]:
        """Get status using subprocess."""
        stdout, stderr, returncode = self._run_git_command(["status", "--porcelain"])
        if returncode != 0:
            return []

        status = []
        for line in stdout.split('\n'):
            if not line.strip():
                continue

            # Parse porcelain format: XY file
            staged_status = line[0]
            unstaged_status = line[1]
            file_path = line[3:]

            # Determine overall status
            if unstaged_status != ' ':
                git_status_str = self._porcelain_to_status(unstaged_status)
                staged = False
            elif staged_status != ' ':
                git_status_str = self._porcelain_to_status(staged_status)
                staged = True
            else:
                continue

            status.append(GitFileStatus(
                path=file_path,
                status=git_status_str,
                staged=staged
            ))

        return status

    def _porcelain_to_status(self, porcelain_char: str) -> str:
        """Convert porcelain status character to status string."""
        mapping = {
            'M': 'modified',
            'A': 'added',
            'D': 'deleted',
            'R': 'renamed',
            'C': 'copied',
            'U': 'updated',
            '?': 'untracked',
            '!': 'ignored'
        }
        return mapping.get(porcelain_char, 'unknown')

    def get_recent_commits(self, since: Optional[datetime] = None, limit: Optional[int] = None) -> List[GitCommit]:
        """Get recent commits, optionally since a specific datetime."""
        max_commits = limit or self.git_config.get("max_commits_history", 100)

        if not self.use_subprocess and self.repo:
            try:
                return self._get_recent_commits_gitpython(since, max_commits)
            except Exception:
                pass

        # Fallback to subprocess
        return self._get_recent_commits_subprocess(since, max_commits)

    def _get_recent_commits_gitpython(self, since: Optional[datetime], limit: int) -> List[GitCommit]:
        """Get recent commits using GitPython."""
        if not self.repo:
            return []

        commits = []
        git_commits = list(self.repo.iter_commits(max_count=limit))

        for commit in git_commits:
            # Check if commit is after the 'since' datetime
            if since and commit.committed_datetime.replace(tzinfo=None) < since:
                continue

            # Get files changed in this commit
            files = []
            if commit.parents:
                # Diff with parent
                diff = commit.parents[0].diff(commit)
                files = [item.a_path for item in diff]
            else:
                # Initial commit, get all files
                files = list(self.repo.git.ls_tree('-r', '--name-only', commit.hexsha).split('\n'))

            commits.append(GitCommit(
                sha=commit.hexsha[:7],
                message=commit.message.strip().split('\n')[0],
                author=commit.author.name,
                timestamp=commit.committed_datetime.replace(tzinfo=None),
                files=files
            ))

        return commits

    def _get_recent_commits_subprocess(self, since: Optional[datetime], limit: int) -> List[GitCommit]:
        """Get recent commits using subprocess."""
        # Format for --since flag
        since_flag = ""
        if since:
            since_flag = since.strftime('%Y-%m-%d %H:%M:%S')

        args = ["log", "--oneline", f"--max-count={limit}", "--name-only"]
        if since_flag:
            args.extend(["--since", since_flag])

        stdout, stderr, returncode = self._run_git_command(args)
        if returncode != 0:
            return []

        commits = []
        lines = stdout.split('\n')
        i = 0

        while i < len(lines):
            line = lines[i].strip()
            if not line:
                i += 1
                continue

            # Parse commit line: sha message
            parts = line.split(' ', 1)
            if len(parts) >= 2:
                sha = parts[0]
                message = parts[1]

                # Collect files until next commit or end
                files = []
                i += 1
                while i < len(lines) and lines[i].strip() and not lines[i].startswith(' '):
                    file_line = lines[i].strip()
                    if file_line:
                        files.append(file_line)
                    i += 1

                # Get commit details
                commit_info = self._get_commit_info_subprocess(sha)
                if commit_info:
                    commits.append(GitCommit(
                        sha=sha,
                        message=message,
                        author=commit_info['author'],
                        timestamp=commit_info['timestamp'],
                        files=files
                    ))
                else:
                    # Fallback with basic info
                    commits.append(GitCommit(
                        sha=sha,
                        message=message,
                        author="unknown",
                        timestamp=datetime.now(),
                        files=files
                    ))
            else:
                i += 1

        return commits

    def _get_commit_info_subprocess(self, sha: str) -> Optional[Dict[str, Any]]:
        """Get detailed commit information using subprocess."""
        stdout, stderr, returncode = self._run_git_command([
            "show", "--format=%an|%ad", "--date=iso", "--no-patch", sha
        ])
        if returncode != 0:
            return None

        line = stdout.strip()
        if '|' not in line:
            return None

        author, date_str = line.split('|', 1)
        try:
            timestamp = datetime.fromisoformat(date_str.replace(' +', '+').replace(' -', '-'))
            return {
                'author': author,
                'timestamp': timestamp
            }
        except ValueError:
            return None

    def is_git_repository(self) -> bool:
        """Check if the path is a git repository."""
        git_dir = self.repo_path / '.git'
        return git_dir.exists() and git_dir.is_dir()


class GitOrchestrator:
    """Orchestrator for the git tracking module."""

    def __init__(self, module_path: Path, parent_config: Optional[Dict[str, Any]] = None):
        self.module_path = module_path
        self.parent_config = parent_config or {}
        self.config_path = module_path / "index.json"
        self.config = self._load_config()

    def _load_config(self) -> Dict[str, Any]:
        """Load configuration from index.json."""
        try:
            with open(self.config_path, 'r') as f:
                return json.load(f)
        except FileNotFoundError:
            print(f"Warning: Git config not found: {self.config_path}")
            return {}
        except json.JSONDecodeError as e:
            print(f"Warning: Invalid git config JSON: {e}")
            return {}

    def create_tracker(self, repo_path: Path) -> GitTracker:
        """Create a GitTracker instance."""
        return GitTracker(repo_path, self.config)


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the git module."""
    try:
        orchestrator = GitOrchestrator(module_path, parent_config)

        # This module is primarily a library - no standalone execution needed
        # The core module will import and use the GitTracker class
        print("Git module loaded successfully")
        return True

    except Exception as e:
        print(f"Git module error: {e}")
        import traceback
        traceback.print_exc()
        return False


# Export the main classes for import by other modules
__all__ = ['GitTracker', 'GitFileStatus', 'GitCommit']