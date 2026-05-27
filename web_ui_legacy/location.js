import { signal } from "./preactive.js";

export const $location = signal("");

export const parsePath = (path) => path.split("/").filter((dir) => dir !== "");

export const navigateTo = (pathIdx) => {
  $location.value = parsePath($location.peek())
    .slice(0, pathIdx + 1)
    .filter((p) => p.length)
    .join("/");
};

export const exitFromDir = () => {
  const dirs = parsePath($location.value);
  if (dirs.length < 1) return;
  $location.value = dirs.slice(0, -1).join("/") || "";
};


export const openEntry = ({ type, name }) => {
  if (type === "dir") {
    $location.value = [$location.peek(), name]
      .filter((p) => p.length)
      .join("/");
  }
};
