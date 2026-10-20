import { $ } from "./dom.js";

export function initializeMediaControls() {
  const music = $("#background-music");
  const musicButton = $("#music-toggle");
  musicButton.addEventListener("click", async () => {
    if (music.paused) {
      try {
        await music.play();
        musicButton.classList.add("active");
      } catch {
        musicButton.title = "Could not play ambience. Check assets/audio/cafe-ambience.m4a.";
      }
    } else {
      music.pause();
      musicButton.classList.remove("active");
    }
  });

  $("#fullscreen-toggle").addEventListener("click", async () => {
    try {
      if (!document.fullscreenElement) await document.documentElement.requestFullscreen();
      else await document.exitFullscreen();
    } catch {
    }
  });
  document.addEventListener("fullscreenchange", () => {
    $("#fullscreen-toggle").classList.toggle("active", !!document.fullscreenElement);
  });
}