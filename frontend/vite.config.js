import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      // Any request to /ch/... is forwarded to ClickHouse HTTP interface.
      // This avoids needing CORS headers on ClickHouse during development.
      '/ch': {
        target: 'http://localhost:8123',
        rewrite: (path) => path.replace(/^\/ch/, '')
      }
    }
  }
})
