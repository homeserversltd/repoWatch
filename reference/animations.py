"""
repoWatch ASCII Art Animation System

Celebrates file changes with animated ASCII art.
"""

import asyncio
import random
import time
from typing import List, Dict, Any, Optional
from dataclasses import dataclass


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


class AnimationEngine:
    """Manages ASCII art animations for file changes."""

    def __init__(self):
        self.animations: Dict[str, Animation] = {}
        self.current_animation: Optional[Animation] = None
        self.current_frame_index: int = 0
        self.last_frame_time: float = 0
        self.is_playing: bool = False

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
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!\n      \\(*^^)/*", 0.5),
                AnimationFrame("🎉 FILE CHANGED!\n   (∩｀-´)⊃━☆ﾟ.*･｡\n\n   ✨ File modified!\n      \\(*^^)/*", 0.5),
            ]
        )

        # File created animation
        self.animations["file_created"] = Animation(
            name="file_created",
            frames=[
                AnimationFrame("✨ NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n", 0.4),
                AnimationFrame("✨ NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   📄 File created!", 0.4),
                AnimationFrame("✨ NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   📄 File created!\n      \\(^o^)/", 0.4),
                AnimationFrame("✨ NEW FILE!\n   (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   📄 File created!\n      \\(^o^)/", 0.4),
            ]
        )

        # Commit animation
        self.animations["commit"] = Animation(
            name="commit",
            frames=[
                AnimationFrame("🚀 COMMIT DETECTED!\n\n   [✓] Commit successful!", 0.6),
                AnimationFrame("🚀 COMMIT DETECTED!\n\n   [✓] Commit successful!\n      (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧", 0.6),
                AnimationFrame("🚀 COMMIT DETECTED!\n\n   [✓] Commit successful!\n      (ﾉ◕ヮ◕)ﾉ*:･ﾟ✧\n\n   🎯 Keep coding!", 0.6),
            ]
        )

        # Directory change animation
        self.animations["directory_change"] = Animation(
            name="directory_change",
            frames=[
                AnimationFrame("📁 DIRECTORY UPDATE\n   📂 Files changed!", 0.5),
                AnimationFrame("📁 DIRECTORY UPDATE\n   📂 Files changed!\n      (╯°□°)╯︵ ┻━┻", 0.5),
                AnimationFrame("📁 DIRECTORY UPDATE\n   📂 Files changed!\n      (╯°□°)╯︵ ┻━┻\n\n   🔥 Busy directory!", 0.5),
            ]
        )

        # Idle animation (shows when no recent activity)
        self.animations["idle"] = Animation(
            name="idle",
            frames=[
                AnimationFrame("💤 Watching...\n   (￣o￣) . z Z", 2.0),
                AnimationFrame("💤 Watching...\n   (￣o￣) . z Z\n\n   👀 Monitoring for changes...", 2.0),
                AnimationFrame("💤 Watching...\n   (-.-) Zzz...", 2.0),
            ],
            loop=True
        )

    def play_animation(self, animation_name: str) -> None:
        """Start playing an animation by name."""
        if animation_name in self.animations:
            self.current_animation = self.animations[animation_name]
            self.current_frame_index = 0
            self.last_frame_time = time.time()
            self.is_playing = True

    def stop_animation(self) -> None:
        """Stop the current animation."""
        self.is_playing = False
        self.current_animation = None
        self.current_frame_index = 0

    def get_current_frame(self) -> Optional[str]:
        """Get the current animation frame content."""
        if not self.is_playing or not self.current_animation:
            return None

        current_time = time.time()

        # Check if it's time to advance to the next frame
        if current_time - self.last_frame_time >= self.current_animation.frames[self.current_frame_index].duration:
            self.current_frame_index += 1
            self.last_frame_time = current_time

            # Check if animation is complete
            if self.current_frame_index >= len(self.current_animation.frames):
                if self.current_animation.loop:
                    self.current_frame_index = 0  # Loop back to start
                else:
                    self.stop_animation()  # End animation
                    return None

        return self.current_animation.frames[self.current_frame_index].content

    def trigger_file_change(self, change_type: str, file_path: str) -> None:
        """Trigger an animation based on file change type."""
        animation_map = {
            "modified": "file_modified",
            "created": "file_created",
            "deleted": "file_modified",  # Use modified animation for deletions too
        }

        if change_type in animation_map:
            self.play_animation(animation_map[change_type])

    def trigger_commit(self, commit_message: str = "") -> None:
        """Trigger commit animation."""
        self.play_animation("commit")

    def trigger_directory_change(self, dir_path: str) -> None:
        """Trigger directory change animation."""
        self.play_animation("directory_change")

    def start_idle_animation(self) -> None:
        """Start the idle watching animation."""
        self.play_animation("idle")

    async def run_animation_loop(self, update_callback):
        """Run the animation loop asynchronously."""
        while True:
            frame = self.get_current_frame()
            if frame is not None:
                update_callback(frame)
            await asyncio.sleep(0.1)  # Update at ~10 FPS

    def get_available_animations(self) -> List[str]:
        """Get list of available animation names."""
        return list(self.animations.keys())

    def get_random_celebration(self) -> str:
        """Get a random celebration message."""
        celebrations = [
            "🎉 Great work!",
            "✨ Nice!",
            "🚀 Keep it up!",
            "💪 You're on fire!",
            "🎯 Bullseye!",
            "🌟 Amazing!",
            "🔥 Hot stuff!",
            "💎 Perfect!",
        ]
        return random.choice(celebrations)


