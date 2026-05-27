import { signal, effect } from "./preactive.js";

export const $errors = signal([]);

let errorTimeout;
effect(() => {
  if (errorTimeout) return;
  errorTimeout = setTimeout(() => {
    if ($errors.value.length > 0) {
      const newArray = [...$errors.value];
      newArray.shift();
      $errors.value = newArray;
    } else {
      errorTimeout = null;
    }
  }, 8000);
});
