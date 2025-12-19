"""
repoWatch Git Tracking System

Tracks git status and commits for repository monitoring.
"""

import os
import subprocess
from datetime import datetime, timedelta
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional, NamedTuple
from dataclasses import dataclass

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

    @property
    def short_sha(self) -> str:
        """Get short SHA (first 8 characters)."""
        return self.sha[:8]

    @property
    def summary(self) -> str:
        """Get commit summary (first line of message)."""
        return self.message.split('\n')[0]


class GitTracker:
    """Tracks git repository status and commits."""

    def __init__(self, repo_path: Path):
        self.repo_path = repo_path.resolve()
        self.repo: Optional[Repo] = None

        if not self.repo_path.exists():
            raise ValueError(f"Repository path does not exist: {self.repo_path}")

        if not (self.repo_path / '.git').exists():
            raise ValueError(f"Not a git repository: {self.repo_path}")

        if GITPYTHON_AVAILABLE:
            try:
                self.repo = Repo(self.repo_path)
            except Exception as e:
                print(f"Warning: Could not initialize GitPython repo: {e}")
                self.repo = None

    def get_status(self) -> List[GitFileStatus]:
        """
        Get the current git status.

        Returns:
            List of GitFileStatus objects for modified files
        """
        if self.repo and GITPYTHON_AVAILABLE:
            return self._get_status_gitpython()
        else:
            return self._get_status_subprocess()

    def _get_status_gitpython(self) -> List[GitFileStatus]:
        """Get status using GitPython."""
        try:
            # Get staged files
            staged_files = []
            if self.repo.git.diff('--cached', '--name-status'):
                staged_output = self.repo.git.diff('--cached', '--name-status')
                for line in staged_output.split('\n'):
                    if line.strip():
                        status, path = line.split('\t', 1)
                        staged_files.append(GitFileStatus(
                            path=path,
                            status=self._normalize_status(status),
                            staged=True
                        ))

            # Get unstaged files
            unstaged_files = []
            if self.repo.git.diff('--name-status'):
                unstaged_output = self.repo.git.diff('--name-status')
                for line in unstaged_output.split('\n'):
                    if line.strip():
                        status, path = line.split('\t', 1)
                        unstaged_files.append(GitFileStatus(
                            path=path,
                            status=self._normalize_status(status),
                            staged=False
                        ))

            # Get untracked files
            untracked = []
            if self.repo.untracked_files:
                for path in self.repo.untracked_files:
                    untracked.append(GitFileStatus(
                        path=path,
                        status='untracked',
                        staged=False
                    ))

            return staged_files + unstaged_files + untracked

        except Exception as e:
            print(f"Warning: GitPython status failed: {e}")
            return self._get_status_subprocess()

    def _get_status_subprocess(self) -> List[GitFileStatus]:
        """Get status using subprocess (fallback)."""
        try:
            # Run git status --porcelain
            result = subprocess.run(
                ['git', 'status', '--porcelain'],
                cwd=self.repo_path,
                capture_output=True,
                text=True,
                timeout=10
            )

            if result.returncode != 0:
                return []

            files = []
            for line in result.stdout.split('\n'):
                if line.strip():
                    # Parse porcelain format: XY file_path
                    status_code = line[:2]
                    path = line[2:].strip()

                    # Determine status and staged state
                    status, staged = self._parse_porcelain_status(status_code)

                    files.append(GitFileStatus(
                        path=path,
                        status=status,
                        staged=staged
                    ))

            return files

        except (subprocess.TimeoutExpired, FileNotFoundError) as e:
            print(f"Warning: git status failed: {e}")
            return []

    def _normalize_status(self, status: str) -> str:
        """Normalize git status codes to readable names."""
        status_map = {
            'A': 'added',
            'M': 'modified',
            'D': 'deleted',
            'R': 'renamed',
            'C': 'copied',
            'U': 'updated',
            '?': 'untracked',
        }
        return status_map.get(status, status.lower())

    def _parse_porcelain_status(self, status_code: str) -> Tuple[str, bool]:
        """Parse git porcelain status code."""
        # First character is staged status, second is unstaged status
        staged_code, unstaged_code = status_code[0], status_code[1]

        # Determine primary status
        if unstaged_code != ' ':
            status = self._normalize_status(unstaged_code)
            staged = False
        elif staged_code != ' ':
            status = self._normalize_status(staged_code)
            staged = True
        else:
            status = 'unknown'
            staged = False

        return status, staged

    def get_recent_commits(self, since: Optional[datetime] = None, limit: int = 50) -> List[GitCommit]:
        """
        Get recent commits.

        Args:
            since: Only commits after this datetime
            limit: Maximum number of commits to return

        Returns:
            List of GitCommit objects
        """
        if self.repo and GITPYTHON_AVAILABLE:
            return self._get_commits_gitpython(since, limit)
        else:
            return self._get_commits_subprocess(since, limit)

    def _get_commits_gitpython(self, since: Optional[datetime], limit: int) -> List[GitCommit]:
        """Get commits using GitPython."""
        try:
            # Get commits
            commits = list(self.repo.iter_commits(
                max_count=limit,
                since=since.isoformat() if since else None
            ))

            result = []
            for commit in commits:
                # Get files changed in this commit
                files = []
                try:
                    # Get diff stats for this commit
                    diff = commit.diff(commit.parents[0] if commit.parents else None, create_patch=False)
                    files = [d.a_path or d.b_path for d in diff]
                except:
                    # Fallback: get all files in commit (less accurate)
                    pass

                result.append(GitCommit(
                    sha=commit.hexsha,
                    message=commit.message,
                    author=commit.author.name,
                    timestamp=datetime.fromtimestamp(commit.authored_date),
                    files=files
                ))

            return result

        except Exception as e:
            print(f"Warning: GitPython commits failed: {e}")
            return self._get_commits_subprocess(since, limit)

    def _get_commits_subprocess(self, since: Optional[datetime], limit: int) -> List[GitCommit]:
        """Get commits using subprocess (fallback)."""
        try:
            # Build git log command
            cmd = ['git', 'log', '--oneline', f'--max-count={limit}']

            if since:
                # Format: 2024-01-01T00:00:00
                since_str = since.strftime('%Y-%m-%dT%H:%M:%S')
                cmd.extend(['--since', since_str])

            cmd.extend(['--pretty=format:%H|%s|%an|%ai'])

            result = subprocess.run(
                cmd,
                cwd=self.repo_path,
                capture_output=True,
                text=True,
                timeout=15
            )

            if result.returncode != 0:
                return []

            commits = []
            for line in result.stdout.split('\n'):
                if line.strip():
                    parts = line.split('|', 3)
                    if len(parts) >= 4:
                        sha, message, author, timestamp_str = parts

                        # Parse timestamp
                        try:
                            timestamp = datetime.strptime(timestamp_str, '%Y-%m-%d %H:%M:%S %z')
                        except ValueError:
                            timestamp = datetime.now()

                        commits.append(GitCommit(
                            sha=sha,
                            message=message,
                            author=author,
                            timestamp=timestamp,
                            files=[]  # Would need separate git show command to get files
                        ))

            return commits

        except (subprocess.TimeoutExpired, FileNotFoundError) as e:
            print(f"Warning: git log failed: {e}")
            return []

    def get_commit_files(self, commit_sha: str) -> List[str]:
        """
        Get list of files changed in a specific commit.

        Args:
            commit_sha: The commit SHA

        Returns:
            List of file paths changed in the commit
        """
        try:
            if self.repo and GITPYTHON_AVAILABLE:
                commit = self.repo.commit(commit_sha)
                files = []

                if commit.parents:
                    diff = commit.diff(commit.parents[0], create_patch=False)
                    files = [d.a_path or d.b_path for d in diff]
                else:
                    # Initial commit - get all files
                    files = [item.path for item in commit.tree.traverse() if item.type == 'blob']

                return files
            else:
                # Use subprocess
                result = subprocess.run(
                    ['git', 'show', '--name-only', '--format=', commit_sha],
                    cwd=self.repo_path,
                    capture_output=True,
                    text=True,
                    timeout=10
                )

                if result.returncode == 0:
                    return [line.strip() for line in result.stdout.split('\n') if line.strip()]

                return []

        except Exception as e:
            print(f"Warning: Could not get commit files for {commit_sha}: {e}")
            return []

    def is_clean(self) -> bool:
        """
        Check if the repository is clean (no uncommitted changes).

        Returns:
            True if repository is clean
        """
        status = self.get_status()
        return len(status) == 0

    def get_branch_name(self) -> str:
        """
        Get the current branch name.

        Returns:
            Current branch name or 'HEAD' if detached
        """
        try:
            if self.repo and GITPYTHON_AVAILABLE:
                return self.repo.active_branch.name
            else:
                result = subprocess.run(
                    ['git', 'branch', '--show-current'],
                    cwd=self.repo_path,
                    capture_output=True,
                    text=True,
                    timeout=5
                )

                if result.returncode == 0:
                    return result.stdout.strip()
                else:
                    return 'HEAD'  # Detached HEAD state

        except Exception as e:
            print(f"Warning: Could not get branch name: {e}")
            return 'unknown'

    def get_remote_url(self) -> Optional[str]:
        """
        Get the remote URL for origin.

        Returns:
            Remote URL or None if not found
        """
        try:
            if self.repo and GITPYTHON_AVAILABLE:
                origin = self.repo.remote('origin')
                return origin.url
            else:
                result = subprocess.run(
                    ['git', 'remote', 'get-url', 'origin'],
                    cwd=self.repo_path,
                    capture_output=True,
                    text=True,
                    timeout=5
                )

                if result.returncode == 0:
                    return result.stdout.strip()

        except Exception as e:
            pass

        return None
