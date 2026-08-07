import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { defineConfig } from 'vite';

const installerRoot = fileURLToPath(new URL('.', import.meta.url));

export default defineConfig({
	base: './',
	build: {
		rollupOptions: {
			input: {
				index: resolve(installerRoot, 'index.html'),
				installer: resolve(installerRoot, 'led-rails.html'),
			},
		},
	},
	plugins: [svelte()],
});
