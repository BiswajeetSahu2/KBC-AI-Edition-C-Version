import { $, historyScreen, showScreen } from "./dom.js";
import { API_BASE } from "./config.js?v=api-8084";
import { session } from "./session.js";

async function accountRequest(path, options = {}) {
  const headers = { ...options.headers, Authorization: `Bearer ${session.token}` };
  if (options.body) headers["Content-Type"] = "application/json";
  const response = await fetch(`${API_BASE}${path}`, { ...options, headers });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || "Account request failed.");
  return result;
}

function showPicture(picture) {
  const image = $("#profile-picture");
  const control = $("#profile-picture-control");
  control.classList.toggle("has-picture", Boolean(picture));
  if (picture) image.src = picture;
  else image.removeAttribute("src");
}

async function makePicture(file) {
  const source = await createImageBitmap(file);
  const canvas = document.createElement("canvas");
  canvas.width = 160;
  canvas.height = 160;
  const context = canvas.getContext("2d");
  const side = Math.min(source.width, source.height);
  context.drawImage(source, (source.width - side) / 2, (source.height - side) / 2, side, side, 0, 0, 160, 160);
  source.close();
  let quality = 0.78;
  let picture = canvas.toDataURL("image/jpeg", quality);
  while (picture.length > 14500 && quality > 0.35) {
    quality -= 0.08;
    picture = canvas.toDataURL("image/jpeg", quality);
  }
  if (picture.length > 15000) throw new Error("That image is too detailed to upload. Try a smaller photo.");
  return picture;
}

function renderHistory(games) {
  const list = $("#history-list");
  list.replaceChildren();
  if (!games.length) {
    $("#history-message").textContent = "No games yet. Your completed and unfinished sessions will appear here.";
    return;
  }
  $("#history-message").textContent = `Showing your ${games.length} most recent game${games.length === 1 ? "" : "s"}.`;
  games.forEach(game => {
    const item = document.createElement("li");
    item.className = "history-item";
    const title = document.createElement("strong");
    title.textContent = `${game.mode === "kbc" ? "KBC Challenge" : game.mode === "practice" ? "Practice" : "Endless Practice"} · ${game.correctAnswers}/${game.totalQuestions || game.correctAnswers} correct`;
    const meta = document.createElement("span");
    const date = new Date(`${game.createdAt.replace(" ", "T")}Z`);
    const status = game.status.charAt(0).toUpperCase() + game.status.slice(1);
    meta.textContent = `${status} · ${Number.isNaN(date.valueOf()) ? game.createdAt : date.toLocaleString()}`;
    item.append(title, meta);
    list.append(item);
  });
}

export function initializeAccount() {
  document.addEventListener("kbc:authenticated", async () => {
    try {
      const result = await accountRequest("/auth/profile-picture");
      showPicture(result.profilePicture);
    } catch {
      showPicture("");
    }
  });

  $("#profile-picture-input").addEventListener("change", async event => {
    const file = event.target.files?.[0];
    event.target.value = "";
    if (!file) return;
    if (!file.type.startsWith("image/")) {
      $("#setup-error").textContent = "Choose an image file for your profile picture.";
      return;
    }
    try {
      const profilePicture = await makePicture(file);
      await accountRequest("/auth/profile-picture", { method: "PUT", body: JSON.stringify({ profilePicture }) });
      showPicture(profilePicture);
      $("#setup-error").textContent = "Profile picture saved.";
    } catch (error) {
      $("#setup-error").textContent = error.message;
    }
  });

  document.addEventListener("kbc:show-history", async () => {
    showScreen(historyScreen);
    $("#history-message").textContent = "Loading your games…";
    $("#history-list").replaceChildren();
    try {
      const result = await accountRequest("/game/history");
      renderHistory(result.games);
    } catch (error) {
      $("#history-message").textContent = error.message;
    }
  });
}
