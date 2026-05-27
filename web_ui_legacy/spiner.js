import { html, signal } from "./preactive.js";

export const $loading = signal(false);

export const LoadingAnimation = () => {
  return html`${$loading.value ? html`<div class="bar active"></div>` : html`<div class="bar"></div>`}`
}