import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'
import path from 'path'

export default defineConfig({
  plugins: [vue()],
  resolve: {
    alias: { '@': path.resolve(__dirname, 'src') }
  },
  server: {
    port: 5173,
    proxy: {
      '/api': 'http://127.0.0.1:7321',
      '/ws': { target: 'ws://127.0.0.1:7321', ws: true }
    }
  },
  build: {
    outDir: '../web-dist',
    emptyOutDir: true
  }
})
