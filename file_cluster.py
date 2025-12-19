"""
repoWatch File Clustering System

Groups files by parent directories for visual compactness.
"""

import os
from pathlib import Path
from typing import Dict, List, Set, Tuple, Optional
from dataclasses import dataclass
from collections import defaultdict


@dataclass
class FileCluster:
    """Represents a cluster of files in the same directory."""
    path: str
    files: List[str]
    count: int
    display_name: str

    def __post_init__(self):
        self.count = len(self.files)


@dataclass
class ClusterGroup:
    """Represents a group of clusters under a common parent."""
    parent_path: str
    clusters: List[FileCluster]
    total_files: int
    display_name: str

    def __post_init__(self):
        self.total_files = sum(cluster.count for cluster in self.clusters)


class FileClusterer:
    """Groups files by directory structure for efficient display."""

    def __init__(self, repo_root: Path, max_depth: int = 3):
        self.repo_root = repo_root.resolve()
        self.max_depth = max_depth

    def cluster_files(self, file_paths: List[str]) -> List[ClusterGroup]:
        """
        Cluster files by their parent directories.

        Args:
            file_paths: List of file paths relative to repo root

        Returns:
            List of ClusterGroup objects sorted by file count
        """
        if not file_paths:
            return []

        # Convert to Path objects and resolve relative to repo root
        resolved_paths = []
        for path_str in file_paths:
            try:
                path = Path(path_str)
                if not path.is_absolute():
                    path = self.repo_root / path
                resolved_paths.append(path.resolve())
            except (ValueError, OSError):
                continue

        # Group files by their parent directories
        dir_to_files: Dict[Path, List[str]] = defaultdict(list)

        for path in resolved_paths:
            if path.exists() and path.is_file():
                # Get relative path from repo root
                try:
                    rel_path = path.relative_to(self.repo_root)
                    parent_dir = rel_path.parent if rel_path.parent != Path('.') else Path('')
                    dir_to_files[parent_dir].append(str(rel_path))
                except ValueError:
                    # File is outside repo root, skip it
                    continue

        # Create clusters
        clusters = []
        for dir_path, files in dir_to_files.items():
            display_name = self._get_display_name(dir_path)
            cluster = FileCluster(
                path=str(dir_path),
                files=sorted(files),
                count=len(files),
                display_name=display_name
            )
            clusters.append(cluster)

        # Group clusters by common parents
        return self._group_clusters(clusters)

    def _get_display_name(self, dir_path: Path) -> str:
        """Get display name for a directory path.
        
        Never shows the repo root - returns empty string for root-level files.
        """
        if str(dir_path) == '' or str(dir_path) == '.':
            return ''  # Root-level files - no parent path to show

        parts = dir_path.parts
        if len(parts) <= self.max_depth:
            return str(dir_path)
        else:
            # Truncate deep paths
            return f"{'/'.join(parts[:self.max_depth])}/..."

    def _group_clusters(self, clusters: List[FileCluster]) -> List[ClusterGroup]:
        """
        Group clusters under common parent directories.
        
        Never groups by repo root - only meaningful directory levels.
        """
        if not clusters:
            return []

        # Sort clusters by file count (descending)
        sorted_clusters = sorted(clusters, key=lambda c: c.count, reverse=True)

        # Create a single group for all clusters
        # Note: parent_path is intentionally empty - we never show repo root
        group = ClusterGroup(
            parent_path="",
            clusters=sorted_clusters,
            total_files=sum(c.count for c in sorted_clusters),
            display_name=""  # Empty - no root-level grouping name
        )

        return [group]

    def cluster_commits(self, commit_files: Dict[str, List[str]]) -> Dict[str, ClusterGroup]:
        """
        Cluster files for each commit.

        Args:
            commit_files: Dict mapping commit SHA to list of files

        Returns:
            Dict mapping commit SHA to ClusterGroup
        """
        result = {}

        for commit_sha, files in commit_files.items():
            clusters = self.cluster_files(files)
            if clusters:
                result[commit_sha] = clusters[0]  # Take the first (and likely only) group

        return result

    def get_directory_stats(self, file_paths: List[str]) -> Dict[str, int]:
        """
        Get statistics about directory distribution.

        Args:
            file_paths: List of file paths

        Returns:
            Dict mapping directory paths to file counts
        """
        clusters = self.cluster_files(file_paths)
        stats = {}

        for group in clusters:
            for cluster in group.clusters:
                stats[cluster.path] = cluster.count

        return stats

    def format_cluster_display(self, cluster: FileCluster, max_files_show: int = 5) -> str:
        """
        Format a cluster for display.

        Args:
            cluster: The cluster to format
            max_files_show: Maximum number of files to show individually

        Returns:
            Formatted string for display
        """
        if cluster.count <= max_files_show:
            # Show all files
            file_lines = []
            for file in cluster.files:
                file_lines.append(f"  ├── {file}")
            files_display = "\n".join(file_lines)
        else:
            # Show first few files and count
            file_lines = []
            for i, file in enumerate(cluster.files[:max_files_show]):
                prefix = "  ├──" if i < max_files_show - 1 else "  └─"
                file_lines.append(f"{prefix} {file}")

            remaining = cluster.count - max_files_show
            if remaining > 0:
                file_lines.append(f"  └─ ... and {remaining} more files")

            files_display = "\n".join(file_lines)

        # For root-level files (no parent path), show files directly without path prefix
        if not cluster.display_name:
            return f"({cluster.count} files)\n{files_display}"
        
        # For files in directories, show directory path
        return f"{cluster.display_name}/... ({cluster.count} files)\n{files_display}"

    def format_group_display(self, group: ClusterGroup) -> str:
        """
        Format a cluster group for display.

        Args:
            group: The group to format

        Returns:
            Formatted string for display
        """
        cluster_lines = []
        for i, cluster in enumerate(group.clusters):
            cluster_display = self.format_cluster_display(cluster)
            cluster_lines.append(cluster_display)

            # Add separator between clusters
            if i < len(group.clusters) - 1:
                cluster_lines.append("")

        return "\n".join(cluster_lines)


def cluster_files_for_display(file_paths: List[str], repo_root: Path) -> str:
    """
    Convenience function to cluster files and format for display.

    Args:
        file_paths: List of file paths
        repo_root: Repository root path

    Returns:
        Formatted string for display
    """
    clusterer = FileClusterer(repo_root)
    groups = clusterer.cluster_files(file_paths)

    if not groups:
        return "No files to display"

    # For now, just return the first group
    return clusterer.format_group_display(groups[0])
