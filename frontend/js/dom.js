export const $ = selector => document.querySelector(selector);
export const authScreen = $("#auth-screen");
export const setupScreen = $("#setup-screen");
export const loadingScreen = $("#loading-screen");
export const gameScreen = $("#game-screen");
export const resultScreen = $("#result-screen");
export const historyScreen = $("#history-screen");
export const subjectInputs = [...document.querySelectorAll('#subject-grid input[type="checkbox"]')];

export function showScreen(screen) {
  [authScreen, setupScreen, loadingScreen, gameScreen, resultScreen, historyScreen].forEach(element => element.classList.add("hidden"));
  screen.classList.remove("hidden");
  document.body.classList.toggle("game-active", screen === gameScreen);
}

export function updateSubjectCount() {
  const selected = subjectInputs.filter(input => input.checked);
  $("#subject-count").textContent = `${selected.length} / 5`;
  subjectInputs.forEach(input => {
    input.disabled = selected.length >= 5 && !input.checked;
  });
}
