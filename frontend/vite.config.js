import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/ch': {
        target: 'http://localhost:8123',
        rewrite: (path) => path.replace(/^\/ch/, '')
      }
    }
  }
})
