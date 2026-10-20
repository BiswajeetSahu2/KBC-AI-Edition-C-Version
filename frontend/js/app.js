import { $, subjectInputs, updateSubjectCount } from "./dom.js";
import { initializeSetup } from "./setup.js";
import { initializeNavigation } from "./navigation.js";
import { initializeMediaControls } from "./media-controls.js";
import { initializeGameFlow } from "./game.js?v=lifeline-polish-1";
import { initializeAuth } from "./auth.js";
import { initializeAccount } from "./account.js";

subjectInputs.forEach(input => input.addEventListener("change", updateSubjectCount));
initializeSetup(initializeGameFlow());
initializeNavigation();
initializeMediaControls();
initializeAccount();
initializeAuth(username => { $("#player-label").textContent = username; });
updateSubjectCount();
