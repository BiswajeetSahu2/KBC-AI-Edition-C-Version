import { $, subjectInputs } from "./dom.js";

export function initializeSetup(startGame) {
  $("#setup-form").addEventListener("submit", event => {
    event.preventDefault();
    const subjects = subjectInputs.filter(input => input.checked).map(input => input.value);
    const player = $("#player-name").value.trim();
    if (subjects.length !== 5) {
      $("#setup-error").textContent = "Select exactly 5 subjects to continue.";
      return;
    }
    if (!player) {
      $("#setup-error").textContent = "Enter a player name.";
      return;
    }
    $("#setup-error").textContent = "";
    startGame({ playerName: player, subjects, mode: $("#game-mode").value });
  });
}