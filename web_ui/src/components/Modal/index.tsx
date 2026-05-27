import type { ComponentChildren } from 'preact';

export function Modal({
  children,
  userCanClose = false,
}: {
  children: ComponentChildren;
  userCanClose?: boolean;
}) {
  return <div>{children}</div>;
}
