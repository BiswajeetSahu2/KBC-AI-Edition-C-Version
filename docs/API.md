# API reference

Base URL: `http://localhost:8084/api`

Protected routes require `Authorization: Bearer <session-token>`. JSON request bodies use `Content-Type: application/json`.

## Accounts

| Method | Route | Auth | Purpose |
| --- | --- | --- | --- |
| POST | `/auth/register` | No | Create an account |
| POST | `/auth/login` | No | Sign in |
| POST | `/auth/guest` | No | Start a guest session |
| GET | `/auth/me` | Yes | Get the current user |
| POST | `/auth/logout` | Yes | End the session |
| POST | `/auth/password-reset` | No | Reset a password with a recovery code |
| POST | `/auth/recovery-code` | Yes | Create or rotate a recovery code |
| GET | `/auth/profile-picture` | Yes | Get the profile picture |
| PUT | `/auth/profile-picture` | Yes | Save a JPEG profile picture (maximum 11 KB) |

## Games

| Method | Route | Auth | Purpose |
| --- | --- | --- | --- |
| POST | `/game/start` | Yes | Start `kbc`, `practice`, or `endless` mode |
| GET | `/game/question?gameId=game-1&questionNumber=1` | Yes | Get the active question |
| POST | `/game/answer` | Yes | Submit an answer (`A`–`D`) or `TIMEOUT` |
| POST | `/game/lifeline` | Yes | Use a KBC lifeline |
| POST | `/game/finish` | Yes | End the active game |
| GET | `/game/history` | Yes | Get recent games for the current account |

`/game/start` expects `playerName`, exactly five `subjects`, and a `mode`. Practice has 50 questions. Endless continues until the player quits. KBC has 15 questions.

## Health check

`GET /health` returns the backend status.
