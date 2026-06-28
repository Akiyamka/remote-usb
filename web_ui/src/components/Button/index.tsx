import clsx from "clsx";
import type { ButtonHTMLAttributes } from "preact";
import styles from "./index.module.css";

export function Button({
  children,
  ...props
}: ButtonHTMLAttributes<HTMLButtonElement> & { ghost: boolean }) {
  const className = [props.class, props.className].filter(Boolean).join(" ");
  const cp = { ...props };
  delete cp.class;
  delete cp.className;
  return (
    <button
      {...cp}
      class={clsx(className, styles["button"], {
        [styles["ghost"]]: props.ghost,
        [styles["regular"]]: !props.ghost,
      })}
    >
      {children}
    </button>
  );
}
