# Deployment notes

This project is set up for local development. The backend uses plain HTTP and binds to all network interfaces. Do not expose it directly to the public internet.

Before deployment:

1. Put the backend behind an HTTPS reverse proxy.
2. Restrict CORS to the deployed frontend origin.
3. Set `KBC_PORT` and a persistent `KBC_DB_PATH` for the host.
4. Set `window.KBC_API_BASE` in `frontend/index.html` to the deployed API URL, ending in `/api`.
5. Add request throttling, backups, and production monitoring.

The app does not send password recovery email. Users reset passwords with their recovery code. Protect the database because it contains account and game data.
