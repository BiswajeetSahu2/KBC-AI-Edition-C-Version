import { $, authScreen, gameScreen, setupScreen, showScreen } from "./dom.js";
import { session } from "./session.js";

function showAuthenticatedSetup() {
  showScreen(session.token ? setupScreen : authScreen);
}

export function initializeNavigation() {
  $("#loading-back-button").addEventListener("click", () => {
    document.dispatchEvent(new Event("kbc:cancel-start"));
    showAuthenticatedSetup();
  });
  $("#play-again-button").addEventListener("click", showAuthenticatedSetup);
  $("#history-back-button").addEventListener("click", showAuthenticatedSetup);
  $("#setup-history-button").addEventListener("click", () => document.dispatchEvent(new Event("kbc:show-history")));
  $("#history-button").addEventListener("click", () => document.dispatchEvent(new Event("kbc:show-history")));
  $("#home-button").addEventListener("click", event => {
    event.preventDefault();
    if (!gameScreen.classList.contains("hidden")) document.dispatchEvent(new Event("kbc:quit"));
    else showAuthenticatedSetup();
  });
}
