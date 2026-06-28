import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { mockApiPlugin } from './dev/mockApiPlugin';

// https://vitejs.dev/config/
export default defineConfig({
	plugins: [mockApiPlugin(), preact()],
});
