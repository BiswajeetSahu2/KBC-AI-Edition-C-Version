import { $, authScreen, setupScreen, showScreen } from "./dom.js";
import { clearSession, restoreSession, saveSession, session } from "./session.js";
import { API_BASE } from "./config.js?v=api-8084";

async function authRequest(path, options = {}, token = "") {
  const headers = { ...options.headers };
  if (options.body) headers["Content-Type"] = "application/json";
  if (token) headers.Authorization = `Bearer ${token}`;
  const response = await fetch(`${API_BASE}${path}`, { ...options, headers });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || "Authentication request failed.");
  return result;
}

function setMode(mode) {
  const registering = mode === "register";
  $("#auth-title").textContent = registering ? "Create your account" : "Welcome back";
  $("#auth-submit-button").textContent = registering ? "Create Account" : "Sign In";
  $("#password-confirm-group").classList.toggle("hidden", !registering);
  $("#auth-password-confirm").disabled = !registering;
  $("#auth-password-confirm").required = registering;
  $("#auth-password").autocomplete = registering ? "new-password" : "current-password";
  $("#show-password-reset").classList.toggle("hidden", registering);
  $("#password-reset-panel").classList.add("hidden");
  $("#login-mode-button").classList.toggle("active", !registering);
  $("#register-mode-button").classList.toggle("active", registering);
  $("#login-mode-button").setAttribute("aria-pressed", String(!registering));
  $("#register-mode-button").setAttribute("aria-pressed", String(registering));
  $("#auth-error").textContent = "";
}

function enterSetup(username, onAuthenticated, isGuest = false) {
  saveSession(session.token, username);
  $("#player-name").value = username;
  $("#player-label").textContent = username;
  $("#sign-out-button").classList.remove("hidden");
  $("#history-button").classList.remove("hidden");
  $("#profile-picture-control").classList.remove("hidden");
  $("#recovery-code-button").classList.toggle("hidden", isGuest);
  $("#setup-recovery-button").classList.toggle("hidden", isGuest);
  showScreen(setupScreen);
  document.dispatchEvent(new Event("kbc:authenticated"));
  onAuthenticated(username);
}

function showRecoveryCode(code) {
  $("#recovery-code-value").textContent = code;
  $("#recovery-dialog").showModal();
}

export function initializeAuth(onAuthenticated) {
  let mode = "login";
  $("#login-mode-button").addEventListener("click", () => {
    mode = "login";
    setMode(mode);
  });
  $("#register-mode-button").addEventListener("click", () => {
    mode = "register";
    setMode(mode);
  });

  $("#auth-form").addEventListener("submit", async event => {
    event.preventDefault();
    const username = $("#auth-username").value.trim();
    const password = $("#auth-password").value;
    if (username.length < 3 || username.length > 24 || !/^[A-Za-z0-9._-]+$/.test(username)) {
      $("#auth-error").textContent = "Use 3-24 letters, numbers, dots, underscores, or hyphens for your username.";
      return;
    }
    if (password.length < 8 || password.length > 128) {
      $("#auth-error").textContent = "Your password must be between 8 and 128 characters.";
      return;
    }
    if (mode === "register" && password !== $("#auth-password-confirm").value) {
      $("#auth-error").textContent = "The passwords do not match.";
      return;
    }
    const submitButton = $("#auth-submit-button");
    submitButton.disabled = true;
    $("#auth-error").textContent = "";
    try {
      const result = await authRequest(`/auth/${mode}`, {
        method: "POST",
        body: JSON.stringify({ username, password })
      });
      session.token = result.sessionToken;
      $("#auth-password").value = "";
      $("#auth-password-confirm").value = "";
      enterSetup(result.username, onAuthenticated, result.isGuest);
      if (result.recoveryCode) showRecoveryCode(result.recoveryCode);
    } catch (error) {
      $("#auth-error").textContent = error.message;
    } finally {
      submitButton.disabled = false;
    }
  });

  $("#guest-button").addEventListener("click", async () => {
    const guestButton = $("#guest-button");
    guestButton.disabled = true;
    $("#auth-error").textContent = "";
    try {
      const result = await authRequest("/auth/guest", { method: "POST" });
      session.token = result.sessionToken;
      $("#auth-password").value = "";
      $("#auth-password-confirm").value = "";
      enterSetup(result.username, onAuthenticated, true);
    } catch (error) {
      $("#auth-error").textContent = error.message;
    } finally {
      guestButton.disabled = false;
    }
  });

  $("#show-password-reset").addEventListener("click", () => {
    $("#password-reset-panel").classList.toggle("hidden");
  });
  $("#reset-password-button").addEventListener("click", async () => {
    const username = $("#reset-username").value.trim();
    const recoveryCode = $("#reset-recovery-code").value.trim().toUpperCase();
    const newPassword = $("#reset-new-password").value;
    if (!username || recoveryCode.length !== 20 || newPassword.length < 8 || newPassword.length > 128) {
      $("#auth-error").textContent = "Enter your username, 20-character recovery code, and a password of at least 8 characters.";
      return;
    }
    const button = $("#reset-password-button");
    button.disabled = true;
    try {
      await authRequest("/auth/password-reset", { method: "POST", body: JSON.stringify({ username, recoveryCode, newPassword }) });
      $("#auth-error").textContent = "Password reset. Sign in with your new password.";
      $("#auth-username").value = username;
      $("#auth-password").value = "";
      $("#reset-recovery-code").value = "";
      $("#reset-new-password").value = "";
      $("#password-reset-panel").classList.add("hidden");
    } catch (error) {
      $("#auth-error").textContent = error.message;
    } finally {
      button.disabled = false;
    }
  });
  const generateRecoveryCode = async () => {
    try {
      const result = await authRequest("/auth/recovery-code", { method: "POST" }, session.token);
      showRecoveryCode(result.recoveryCode);
    } catch (error) {
      $("#setup-error").textContent = error.message;
    }
  };
  $("#setup-recovery-button").addEventListener("click", generateRecoveryCode);
  $("#recovery-code-button").addEventListener("click", generateRecoveryCode);
  $("#close-recovery-dialog").addEventListener("click", () => $("#recovery-dialog").close());
  $("#copy-recovery-code").addEventListener("click", async () => {
    try {
      await navigator.clipboard.writeText($("#recovery-code-value").textContent);
      $("#copy-recovery-code").textContent = "Copied";
    } catch {
      $("#copy-recovery-code").textContent = "Select and copy the code";
    }
  });

  $("#sign-out-button").addEventListener("click", async () => {
    try {
      await authRequest("/auth/logout", { method: "POST" }, session.token);
    } catch {
    }
    clearSession();
    $("#player-label").textContent = "Guest";
    $("#sign-out-button").classList.add("hidden");
    $("#history-button").classList.add("hidden");
    $("#profile-picture-control").classList.add("hidden");
    $("#recovery-code-button").classList.add("hidden");
    $("#auth-username").value = "";
    $("#auth-password").value = "";
    mode = "login";
    setMode(mode);
    showScreen(authScreen);
  });

  if (!restoreSession()) {
    showScreen(authScreen);
    return;
  }
  authRequest("/auth/me", {}, session.token)
    .then(user => enterSetup(user.username, onAuthenticated, user.isGuest))
    .catch(() => {
      clearSession();
      showScreen(authScreen);
    });
}
