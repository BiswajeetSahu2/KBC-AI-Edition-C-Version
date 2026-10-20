const tokenKey = "kbc.sessionToken";
const usernameKey = "kbc.username";

export const session = { token: "", username: "" };

export function saveSession(token, username) {
  session.token = token;
  session.username = username;
  localStorage.setItem(tokenKey, token);
  localStorage.setItem(usernameKey, username);
}

export function restoreSession() {
  session.token = localStorage.getItem(tokenKey) || "";
  session.username = localStorage.getItem(usernameKey) || "";
  return session.token;
}

export function clearSession() {
  session.token = "";
  session.username = "";
  localStorage.removeItem(tokenKey);
  localStorage.removeItem(usernameKey);
}
