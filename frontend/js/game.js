import { $, authScreen, gameScreen, loadingScreen, resultScreen, setupScreen, showScreen } from "./dom.js";
import { clearSession, session } from "./session.js";
import { API_BASE } from "./config.js?v=api-8084";
const letters = ["A", "B", "C", "D"];
const prizes = [1000, 2000, 3000, 5000, 10000, 20000, 40000, 80000, 160000, 320000, 640000, 1250000, 2500000, 5000000, 10000000];
const game = { id: "", mode: "", player: "", total: 0, questionNumber: 1, correctAnswers: 0, answeredQuestions: 0, timer: null, locked: false, subjects: [], usedLifelines: new Set(), eliminatedOptions: new Set(), doubleDipRetry: false };

async function request(path, options = {}) {
  const headers = { ...options.headers };
  if (options.body) headers["Content-Type"] = "application/json";
  if (session.token) headers.Authorization = `Bearer ${session.token}`;
  const response = await fetch(`${API_BASE}${path}`, {
    ...options,
    headers
  });
  const payload = await response.json();
  if (!response.ok) {
    if (response.status === 401 && session.token) {
      clearSession();
      $("#player-label").textContent = "Guest";
      $("#sign-out-button").classList.add("hidden");
      showScreen(authScreen);
    }
    const error = new Error(payload.error || "The quiz service returned an error.");
    error.status = response.status;
    throw error;
  }
  return payload;
}

function renderPrizeLadder() {
  const list = $("#prize-list");
  list.replaceChildren();
  prizes.forEach((prize, index) => {
    const item = document.createElement("li");
    if ([4, 9, 14].includes(index)) item.classList.add("safe");
    if (index === game.questionNumber - 1) item.classList.add("active");
    const amount = document.createElement("span");
    amount.textContent = `₹ ${prize.toLocaleString("en-IN")}`;
    const number = document.createElement("span");
    number.textContent = String(index + 1);
    item.append(amount, number);
    list.append(item);
  });
}

function formatPrize(correctAnswers, final = false) {
  if (game.mode !== "kbc" || correctAnswers < 1) return "₹ 0";
  const index = final
    ? correctAnswers >= 15 ? 14 : correctAnswers >= 10 ? 9 : correctAnswers >= 5 ? 4 : -1
    : Math.min(correctAnswers - 1, prizes.length - 1);
  return index < 0 ? "₹ 0" : `₹ ${prizes[index].toLocaleString("en-IN")}`;
}

function setTimer() {
  clearInterval(game.timer);
  let remaining = 30;
  const label = $("#timer-label");
  const ring = $("#timer-ring");
  label.textContent = String(remaining);
  ring.classList.remove("urgent");
  game.timer = setInterval(() => {
    remaining -= 1;
    label.textContent = String(remaining);
    ring.classList.toggle("urgent", remaining <= 10);
    if (remaining <= 0) submitAnswer("TIMEOUT");
  }, 1000);
}

function showAnswers(options) {
  game.eliminatedOptions = new Set();
  const grid = $("#answer-grid");
  grid.replaceChildren();
  letters.forEach(letter => {
    const button = document.createElement("button");
    button.className = "answer-button";
    button.type = "button";
    button.dataset.answer = letter;
    const key = document.createElement("b");
    key.textContent = `${letter}.`;
    button.append(key, document.createTextNode(` ${options[letter]}`));
    grid.append(button);
  });
}

function updateLifelines() {
  const controls = $("#lifeline-controls");
  controls.classList.toggle("hidden", game.mode !== "kbc");
  controls.querySelectorAll("button[data-lifeline]").forEach(button => {
    button.disabled = game.locked || game.doubleDipRetry || game.usedLifelines.has(button.dataset.lifeline);
  });
  const select = $("#flip-subject");
  if (!select.options.length) {
    game.subjects.forEach(subject => {
      const option = document.createElement("option");
      option.value = subject;
      option.textContent = subject;
      select.append(option);
    });
  }
  select.disabled = game.locked || game.doubleDipRetry || game.usedLifelines.has("flip");
}

async function loadQuestion() {
  game.locked = false;
  game.doubleDipRetry = false;
  $("#next-question-button").classList.add("hidden");
  $("#quit-button").classList.remove("hidden");
  $("#game-message").textContent = "Choose an answer before the timer runs out.";
  try {
    const question = await request(`/game/question?gameId=${encodeURIComponent(game.id)}&questionNumber=${game.questionNumber}`);
    $("#question-subject").textContent = question.subject;
    $("#question-number").textContent = `QUESTION ${question.questionNumber}`;
    $("#question-text").textContent = question.question;
    $("#difficulty-label").textContent = question.difficulty;
    $("#progress-caption").textContent = game.mode === "kbc" ? "PADAV" : "PROGRESS";
    $("#padav-label").textContent = `${game.questionNumber} / ${game.total || "∞"}`;
    $("#prize-panel").classList.toggle("hidden", game.mode !== "kbc");
    gameScreen.classList.toggle("no-prize", game.mode !== "kbc");
    $("#player-label").textContent = game.player;
    $("#prize-label").textContent = formatPrize(game.correctAnswers);
    if (game.mode === "kbc") renderPrizeLadder();
    showAnswers(question.options);
    updateLifelines();
    setTimer();
  } catch (error) {
    clearInterval(game.timer);
    $("#game-message").textContent = error.message;
    $("#next-question-button").textContent = "Retry Question";
    $("#next-question-button").dataset.retryQuestion = "true";
    delete $("#next-question-button").dataset.returnToSetup;
    $("#next-question-button").dataset.finish = "false";
    $("#next-question-button").classList.remove("hidden");
    game.locked = true;
    updateLifelines();
  }
}

async function submitAnswer(answer) {
  if (game.locked) return;
  game.locked = true;
  clearInterval(game.timer);
  const buttons = [...$("#answer-grid").querySelectorAll("button")];
  game.pendingAnswer = answer;
  const unavailableOptions = new Set(game.eliminatedOptions);
  buttons.forEach(button => { button.disabled = true; });
  try {
    const result = await request("/game/answer", {
      method: "POST",
      body: JSON.stringify({ gameId: game.id, questionNumber: game.questionNumber, answer })
    });
    if (result.retryAllowed) {
      game.pendingAnswer = null;
      game.doubleDipRetry = true;
      game.locked = false;
      buttons.forEach(button => {
        button.disabled = button.dataset.answer === answer || unavailableOptions.has(button.dataset.answer);
      });
      const chosen = buttons.find(button => button.dataset.answer === answer);
      if (chosen) { chosen.disabled = true; chosen.classList.add("wrong"); }
      $("#game-message").textContent = "That answer is incorrect. Double Dip gives you one more try.";
      setTimer();
      updateLifelines();
      return;
    }
    game.doubleDipRetry = false;
    game.pendingAnswer = null;
    game.correctAnswers = result.correctAnswers;
    game.answeredQuestions += 1;
    buttons.forEach(button => {
      if (button.dataset.answer === result.correctAnswer) button.classList.add("correct");
      else if (button.dataset.answer === answer) button.classList.add("wrong");
    });
    $("#prize-label").textContent = formatPrize(game.correctAnswers, result.gameOver);
    $("#game-message").textContent = `${result.correct ? "Correct." : answer === "TIMEOUT" ? "Time is up." : "Not quite."} ${result.explanation}`;
    const gameOver = game.mode === "kbc"
      ? result.gameOver
      : game.mode === "practice" && game.questionNumber >= game.total;
    const nextButton = $("#next-question-button");
    delete nextButton.dataset.returnToSetup;
    nextButton.textContent = gameOver ? "View Results" : "Next Question";
    nextButton.dataset.finish = gameOver ? "true" : "false";
    nextButton.classList.remove("hidden");
    updateLifelines();
  } catch (error) {
    $("#game-message").textContent = error.message;
    const nextButton = $("#next-question-button");
    nextButton.textContent = "Retry Answer";
    nextButton.dataset.retryAnswer = "true";
    delete nextButton.dataset.returnToSetup;
    nextButton.dataset.finish = "false";
    nextButton.classList.remove("hidden");
    updateLifelines();
  }
}

async function showResults(status = "completed") {
  clearInterval(game.timer);
  try {
    const result = await request("/game/finish", {
      method: "POST",
      body: JSON.stringify({ gameId: game.id })
    });
    game.correctAnswers = result.correctAnswers;
  } catch (error) {
    $("#game-message").textContent = error.message;
  }
  const answered = game.answeredQuestions;
  const total = game.total || answered;
  const prize = formatPrize(game.correctAnswers, status !== "quit");
  $("#result-title").textContent = status === "quit" ? "Session Ended" : game.correctAnswers === game.total && game.total ? "Challenge Complete" : "Quiz Complete";
  $("#result-summary").textContent = `${game.player}, you answered ${answered} question${answered === 1 ? "" : "s"}. ${game.correctAnswers} answer${game.correctAnswers === 1 ? " was" : "s were"} correct.`;
  $("#result-prize").textContent = prize;
  $("#prize-label").textContent = prize;
  $("#result-correct").textContent = `${game.correctAnswers} / ${total}`;
  showScreen(resultScreen);
}

async function returnToSetup() {
  clearInterval(game.timer);
  try {
    if (game.id) await request("/game/finish", { method: "POST", body: JSON.stringify({ gameId: game.id }) });
  } catch {
    // Keep setup available if the service cannot close the session.
  }
  showScreen(setupScreen);
}

async function retryPendingAnswer() {
  const answer = game.pendingAnswer;
  if (!answer) return;
  const nextButton = $("#next-question-button");
  nextButton.disabled = true;
  try {
    await request(`/game/question?gameId=${encodeURIComponent(game.id)}&questionNumber=${game.questionNumber}`);
    delete nextButton.dataset.retryAnswer;
    game.locked = false;
    $("#answer-grid").querySelectorAll("button[data-answer]").forEach(button => {
      button.disabled = game.eliminatedOptions.has(button.dataset.answer);
    });
    await submitAnswer(answer);
  } catch (error) {
    if (error.status === 409) {
      const nextNumber = game.questionNumber + 1;
      try {
        await request(`/game/question?gameId=${encodeURIComponent(game.id)}&questionNumber=${nextNumber}`);
        game.questionNumber = nextNumber;
        game.answeredQuestions += 1;
        game.pendingAnswer = null;
        delete nextButton.dataset.retryAnswer;
        await loadQuestion();
      } catch (nextError) {
        if (nextError.status === 409) {
          game.answeredQuestions += 1;
          game.pendingAnswer = null;
          delete nextButton.dataset.retryAnswer;
          await showResults("completed");
        } else {
          $("#game-message").textContent = nextError.message;
        }
      }
    } else {
      $("#game-message").textContent = error.message;
    }
  } finally {
    nextButton.disabled = false;
  }
}

export function initializeGameFlow() {
  let startController = null;
  document.addEventListener("kbc:cancel-start", () => {
    startController?.abort();
    startController = null;
  });
  $("#answer-grid").addEventListener("click", event => {
    const button = event.target.closest("button[data-answer]");
    if (button && !button.disabled) submitAnswer(button.dataset.answer);
  });
  $("#lifeline-controls").addEventListener("click", async event => {
    const button = event.target.closest("button[data-lifeline]");
    if (!button || button.disabled || game.locked) return;
    const lifeline = button.dataset.lifeline;
    button.disabled = true;
    try {
      const result = await request("/game/lifeline", {
        method: "POST",
        body: JSON.stringify({ gameId: game.id, lifeline, ...(lifeline === "flip" ? { subject: $("#flip-subject").value } : {}) })
      });
      game.usedLifelines.add(lifeline);
      if (lifeline === "fifty_fifty") {
        result.removedOptions.forEach(letter => {
          const option = $("#answer-grid").querySelector(`[data-answer="${letter}"]`);
          if (option) {
            option.disabled = true;
            option.classList.add("wrong");
            game.eliminatedOptions.add(letter);
          }
        });
        $("#game-message").textContent = "50:50 removed two incorrect options.";
      } else if (lifeline === "expert") {
        $("#game-message").textContent = `The expert says ${result.correctAnswer} is correct. ${result.explanation}`;
      } else if (lifeline === "flip") {
        $("#question-subject").textContent = result.subject;
        $("#question-number").textContent = `QUESTION ${result.questionNumber}`;
        $("#question-text").textContent = result.question;
        $("#difficulty-label").textContent = result.difficulty;
        showAnswers(result.options);
        $("#game-message").textContent = "Your question has been replaced.";
      } else {
        $("#game-message").textContent = "Double Dip is ready: you may try one more answer if your first is wrong.";
      }
      updateLifelines();
    } catch (error) {
      $("#game-message").textContent = error.message;
      button.disabled = false;
    }
  });
  $("#next-question-button").addEventListener("click", () => {
    if (!game.id) {
      showScreen(setupScreen);
    } else if ($("#next-question-button").dataset.returnToSetup === "true") {
      returnToSetup();
    } else if ($("#next-question-button").dataset.retryQuestion === "true") {
      delete $("#next-question-button").dataset.retryQuestion;
      loadQuestion();
    } else if ($("#next-question-button").dataset.retryAnswer === "true") {
      retryPendingAnswer();
    } else if ($("#next-question-button").dataset.finish === "true") {
      showResults("completed");
    } else {
      game.questionNumber += 1;
      loadQuestion();
    }
  });
  $("#quit-button").addEventListener("click", () => showResults("quit"));
  document.addEventListener("kbc:quit", () => showResults("quit"));

  return async ({ playerName, subjects, mode }) => {
    startController?.abort();
    const controller = new AbortController();
    startController = controller;
    showScreen(loadingScreen);
    $("#setup-error").textContent = "";
    try {
      const session = await request("/game/start", {
        method: "POST",
        body: JSON.stringify({ playerName, subjects, mode }),
        signal: controller.signal
      });
      if (controller.signal.aborted) return;
      Object.assign(game, {
        id: session.gameId,
        mode,
        player: session.playerName,
        total: session.questionCount,
        questionNumber: 1,
        correctAnswers: 0,
        answeredQuestions: 0,
        locked: false,
        subjects,
        usedLifelines: new Set(),
        eliminatedOptions: new Set(),
        doubleDipRetry: false
      });
      $("#flip-subject").replaceChildren();
      delete $("#next-question-button").dataset.returnToSetup;
      delete $("#next-question-button").dataset.retryQuestion;
      delete $("#next-question-button").dataset.retryAnswer;
      gameScreen.classList.toggle("no-prize", mode !== "kbc");
      $("#prize-panel").classList.toggle("hidden", mode !== "kbc");
      $("#result-title").textContent = "Session Ended";
      showScreen(gameScreen);
      await loadQuestion();
    } catch (error) {
      if (error.name === "AbortError") return;
      if (!session.token) {
        showScreen(authScreen);
        return;
      }
      $("#setup-error").textContent = `${error.message} Check that the backend is running at ${API_BASE}.`;
      showScreen(setupScreen);
    } finally {
      if (startController === controller) startController = null;
    }
  };
}
