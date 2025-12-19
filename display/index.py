#!/usr/bin/env python3
"""
repoWatch Display Module

File clustering and ASCII animation display system.
"""

import asyncio
import random
import time
from typing import List, Dict, Any, Optional
from dataclasses import dataclass
from pathlib import Path
import os
import json
import sys
from collections import defaultdict


@dataclass
class AnimationFrame:
    """Represents a single frame in an animation sequence."""
    content: str
    duration: float  # seconds to display this frame


@dataclass
class Animation:
    """Represents a complete animation sequence."""
    name: str
    frames: List[AnimationFrame]
    loop: bool = False


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


class AnimationEngine:
    """Manages ASCII art animations for file changes."""

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        self.config = config or {}
        self.animations: Dict[str, Animation] = {}
        self.current_animation: Optional[Animation] = None
        self.current_frame_index: int = 0
        self.last_frame_time: float = 0
        self.is_playing: bool = False

        self.animation_config = self.config.get("config", {}).get("animation_settings", {})
        self.fps = self.animation_config.get("fps", 10)
        self.frame_duration = self.animation_config.get("frame_duration", 0.1)

        self._load_animations()

    def _load_animations(self):
        """Load all available animations."""

        # File modified animation
        self.animations["file_modified"] = Animation(
            name="file_modified",
            frames=[
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n", 0.5),
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!", 0.5),
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!\n      \\(*^^)/*", 0.5),
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!\n      \\(*^^)/*\n         ★", 0.5),
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!\n      \\(*^^)/*\n         ★ ★", 0.5),
            ],
            loop=False
        )

        # File created animation
        self.animations["file_created"] = Animation(
            name="file_created",
            frames=[
                AnimationFrame("🆕 NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n", 0.5),
                AnimationFrame("🆕 NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   🌟 File created!", 0.5),
                AnimationFrame("🆕 NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   🌟 File created!\n      \\(°o°)/", 0.5),
                AnimationFrame("🆕 NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   🌟 File created!\n      \\(°o°)/\n         ⚡", 0.5),
            ],
            loop=False
        )

        # Idle animation
        idle_frames = []
        for i in range(20):
            dots = "." * ((i % 4) + 1)
            frame = AnimationFrame(f"💤 Watching for changes{dots}", 0.3)
            idle_frames.append(frame)

        self.animations["idle"] = Animation(
            name="idle",
            frames=idle_frames,
            loop=True
        )

    def trigger_file_change(self, change_type: str, file_path: str):
        """Trigger an animation for a file change."""
        animation_name = f"file_{change_type}"
        if animation_name in self.animations:
            self.current_animation = self.animations[animation_name]
            self.current_frame_index = 0
            self.last_frame_time = time.time()
            self.is_playing = True

    def start_idle_animation(self):
        """Start the idle animation."""
        if not self.is_playing:
            self.current_animation = self.animations.get("idle")
            self.current_frame_index = 0
            self.last_frame_time = time.time()
            self.is_playing = True

    def get_current_frame(self) -> Optional[str]:
        """Get the current animation frame."""
        if not self.current_animation or not self.is_playing:
            return None

        now = time.time()
        if now - self.last_frame_time >= self.current_animation.frames[self.current_frame_index].duration:
            # Move to next frame
            self.current_frame_index += 1
            self.last_frame_time = now

            # Check if animation is complete
            if self.current_frame_index >= len(self.current_animation.frames):
                if self.current_animation.loop:
                    self.current_frame_index = 0
                else:
                    self.is_playing = False
                    return None

        return self.current_animation.frames[self.current_frame_index].content

    def stop_animation(self):
        """Stop the current animation."""
        self.is_playing = False
        self.current_animation = None


class FileClusterer:
    """Groups files by directory structure for efficient display."""

    def __init__(self, repo_root: Path, config: Optional[Dict[str, Any]] = None):
        self.repo_root = repo_root.resolve()
        self.config = config or {}
        self.clustering_config = self.config.get("config", {}).get("clustering_settings", {})

        self.max_depth = self.clustering_config.get("max_depth", 3)
        self.max_files_per_cluster = self.clustering_config.get("max_files_per_cluster", 10)
        self.cluster_separator = self.clustering_config.get("cluster_separator", " › ")
        self.show_hidden = self.clustering_config.get("show_hidden_files", False)
        self.sort_by = self.clustering_config.get("sort_by", "name")

    def cluster_files(self, file_paths: List[str]) -> List[ClusterGroup]:
        """
        Cluster files by their parent directories.

        Args:
            file_paths: List of file paths relative to repo root

        Returns:
            List of ClusterGroup objects
        """
        if not file_paths:
            return []

        # Convert to Path objects and filter
        paths = []
        for file_path in file_paths:
            try:
                path = Path(file_path)
                if not self.show_hidden and path.name.startswith('.'):
                    continue
                paths.append(path)
            except (ValueError, OSError):
                continue

        # Group by directory at different depths
        groups = defaultdict(list)

        for path in paths:
            # Try different directory depths
            for depth in range(self.max_depth + 1):
                try:
                    parent = path.parents[depth] if depth > 0 else Path(".")
                    parent_str = str(parent)

                    # Skip if we have too many files in this cluster
                    if len(groups[parent_str]) >= self.max_files_per_cluster:
                        continue

                    groups[parent_str].append(str(path))
                    break  # Found a suitable cluster

                except IndexError:
                    # Path doesn't have enough parents, use root
                    groups["."].append(str(path))
                    break

        # Convert to ClusterGroup objects
        cluster_groups = []
        for parent_path, files in groups.items():
            # Sort files
            if self.sort_by == "name":
                files.sort()
            elif self.sort_by == "modified":
                try:
                    files.sort(key=lambda f: os.path.getmtime(f), reverse=True)
                except OSError:
                    files.sort()

            # Create file clusters within this group
            clusters = self._create_clusters(parent_path, files)

            if clusters:
                group = ClusterGroup(
                    parent_path=parent_path,
                    clusters=clusters,
                    total_files=len(files),
                    display_name=self._format_group_name(parent_path)
                )
                cluster_groups.append(group)

        # Sort groups by total files (descending)
        cluster_groups.sort(key=lambda g: g.total_files, reverse=True)

        return cluster_groups

    def _create_clusters(self, parent_path: str, files: List[str]) -> List[FileCluster]:
        """Create FileCluster objects for a group."""
        clusters = []

        # If few files, create one cluster
        if len(files) <= self.max_files_per_cluster:
            cluster = FileCluster(
                path=parent_path,
                files=files,
                count=len(files),
                display_name=self._format_cluster_name(parent_path, len(files))
            )
            clusters.append(cluster)
        else:
            # Split into multiple clusters
            for i in range(0, len(files), self.max_files_per_cluster):
                chunk = files[i:i + self.max_files_per_cluster]
                cluster_name = f"{parent_path} (part {i // self.max_files_per_cluster + 1})"
                cluster = FileCluster(
                    path=cluster_name,
                    files=chunk,
                    count=len(chunk),
                    display_name=self._format_cluster_name(cluster_name, len(chunk))
                )
                clusters.append(cluster)

        return clusters

    def _format_group_name(self, parent_path: str) -> str:
        """Format a group name for display."""
        if parent_path == ".":
            return "Root Directory"
        return parent_path

    def _format_cluster_name(self, path: str, count: int) -> str:
        """Format a cluster name for display."""
        display_config = self.config.get("config", {}).get("display_formats", {})
        count_format = display_config.get("file_count_format", "({count} files)")
        cluster_prefix = display_config.get("cluster_prefix", "📁 ")

        return f"{cluster_prefix}{path} {count_format.format(count=count)}"


def display_all_files_flat(file_paths: List[str], repo_root: Path,
                          config: Optional[Dict[str, Any]] = None) -> str:
    """
    Display all files as a flat list without clustering.

    Args:
        file_paths: List of file paths
        repo_root: Repository root path
        config: Configuration dictionary

    Returns:
        Formatted display string with all files listed individually
    """
    if not file_paths:
        return "No files to display"

    # Get display configuration
    display_config = config.get("config", {}).get("display_formats", {}) if config else {}
    file_prefix = display_config.get("file_prefix", "📄 ")

    # Convert to Path objects and filter/sort
    paths = []
    for file_path in file_paths:
        try:
            path = Path(file_path)
            if not display_config.get("show_hidden_files", False) and path.name.startswith('.'):
                continue
            paths.append(path)
        except (ValueError, OSError):
            continue

    # Sort by name
    paths.sort()

    # Format each file with prefix
    lines = []
    for path in paths:
        lines.append(f"{file_prefix}{str(path)}")

    return "\n".join(lines)


def cluster_files_for_display(file_paths: List[str], repo_root: Path,
                            config: Optional[Dict[str, Any]] = None) -> str:
    """
    Cluster files and format for display.

    Args:
        file_paths: List of file paths
        repo_root: Repository root path
        config: Configuration dictionary

    Returns:
        Formatted display string
    """
    clusterer = FileClusterer(repo_root, config)
    groups = clusterer.cluster_files(file_paths)

    if not groups:
        return "No files to display"

    lines = []
    for group in groups:
        lines.append(f"[{group.display_name}] ({group.total_files} files)")

        for cluster in group.clusters:
            lines.append(f"  {cluster.display_name}")

            # Show individual files if not too many
            if cluster.count <= 10:  # Configurable limit
                display_config = config.get("config", {}).get("display_formats", {}) if config else {}
                file_prefix = display_config.get("file_prefix", "📄 ")

                for file in cluster.files[:10]:  # Limit display
                    lines.append(f"    {file_prefix}{file}")

                if cluster.count > 10:
                    lines.append(f"    ... and {cluster.count - 10} more files")

    return "\n".join(lines)


class DisplayOrchestrator:
    """Orchestrator for the display module."""

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
            print(f"Warning: Display config not found: {self.config_path}")
            return {}
        except json.JSONDecodeError as e:
            print(f"Warning: Invalid display config JSON: {e}")
            return {}

    def create_clusterer(self, repo_root: Path) -> FileClusterer:
        """Create a FileClusterer instance."""
        return FileClusterer(repo_root, self.config)

    def create_animation_engine(self) -> AnimationEngine:
        """Create an AnimationEngine instance."""
        return AnimationEngine(self.config)


def main(module_path: Path, parent_config: Optional[Dict[str, Any]] = None) -> bool:
    """Entry point for the display module."""
    try:
        orchestrator = DisplayOrchestrator(module_path, parent_config)

        # This module is primarily a library - no standalone execution needed
        # The core module will import and use the display classes
        print("Display module loaded successfully")
        return True

    except Exception as e:
        print(f"Display module error: {e}")
        import traceback
        traceback.print_exc()
        return False


# Export the main classes for import by other modules
__all__ = ['FileClusterer', 'AnimationEngine', 'cluster_files_for_display', 'display_all_files_flat', 'FileCluster', 'ClusterGroup']
