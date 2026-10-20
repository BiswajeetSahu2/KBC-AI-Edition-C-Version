# KBC AI EDITION

A browser-based quiz game with a C backend and SQLite database. It includes 50-question Practice, Endless Practice, accounts, game history, profile pictures, recovery codes, and KBC lifelines.

## Run on Windows

Requirements: GCC and Python 3 on `PATH`.

1. In Terminal 1, build and start the backend:

   ```powershell
   cd .\backend
   .\build.ps1
   .\kbc-backend.exe
   ```

   The API listens on `http://localhost:8084`. Keep this terminal open. Stop it with Ctrl+C before rebuilding.

2. In Terminal 2, serve the frontend:

   ```powershell
   cd .\frontend
   python -m http.server 5173
   ```

3. Open `http://localhost:5173/`.

The backend creates `backend/kbc.db` automatically. Check `http://localhost:8084/api/health` to confirm it is running.

## Configuration

- Backend port: set `KBC_PORT` before starting the executable. Default: `8084`.
- Database path: set `KBC_DB_PATH`. Default: `kbc.db` in the backend working directory.
- Frontend API address: set `window.KBC_API_BASE` before the module script in `frontend/index.html`. Default: port `8084` on the page's hostname.

## Documentation

- [API reference](docs/API.md)
- [Deployment notes](docs/DEPLOYMENT.md)

Generated build files, the executable, and the local database are excluded by `.gitignore`.
