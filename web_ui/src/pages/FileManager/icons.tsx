import type { ComponentChildren } from "preact";
import sharedStyles from "./shared.module.css";

type IconProps = {
  size?: number;
};

function SvgIcon({
  children,
  size = 20,
  fill = "none",
}: IconProps & {
  children: ComponentChildren;
  fill?: string;
}) {
  return (
    <svg
      aria-hidden="true"
      class={sharedStyles["icon"]}
      fill={fill}
      height={size}
      stroke="currentColor"
      strokeLinecap="round"
      strokeLinejoin="round"
      strokeWidth="2"
      viewBox="0 0 24 24"
      width={size}
      xmlns="http://www.w3.org/2000/svg"
    >
      {children}
    </svg>
  );
}

export function FileIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z" />
      <polyline points="13 2 13 9 20 9" />
    </SvgIcon>
  );
}

export function FolderIcon(props: IconProps) {
  return (
    <SvgIcon fill="currentColor" {...props}>
      <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
    </SvgIcon>
  );
}

export function FolderPlusIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z" />
      <line x1="12" x2="12" y1="10" y2="18" />
      <line x1="8" x2="16" y1="14" y2="14" />
    </SvgIcon>
  );
}

export function HomeIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M3 11 12 3l9 8" />
      <path d="M5 10v10h14V10" />
      <path d="M9 20v-6h6v6" />
    </SvgIcon>
  );
}

export function UploadIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
      <polyline points="17 8 12 3 7 8" />
      <line x1="12" x2="12" y1="3" y2="15" />
    </SvgIcon>
  );
}

export function DownloadIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4" />
      <polyline points="7 10 12 15 17 10" />
      <line x1="12" x2="12" y1="15" y2="3" />
    </SvgIcon>
  );
}

export function TrashIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <polyline points="3 6 5 6 21 6" />
      <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6" />
      <path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2" />
    </SvgIcon>
  );
}

export function MoreIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <circle cx="12" cy="12" r="1" />
      <circle cx="19" cy="12" r="1" />
      <circle cx="5" cy="12" r="1" />
    </SvgIcon>
  );
}

export function EditIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <path d="M12 20h9" />
      <path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4Z" />
    </SvgIcon>
  );
}

export function ArrowUpIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <polyline points="14 9 9 4 4 9" />
      <path d="M20 20h-7a4 4 0 0 1-4-4V4" />
    </SvgIcon>
  );
}

export function CloseIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <line x1="18" x2="6" y1="6" y2="18" />
      <line x1="6" x2="18" y1="6" y2="18" />
    </SvgIcon>
  );
}

export function CheckIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <polyline points="20 6 9 17 4 12" />
    </SvgIcon>
  );
}

export function CancelIcon(props: IconProps) {
  return (
    <SvgIcon {...props}>
      <circle cx="12" cy="12" r="10" />
      <line x1="15" y1="9" x2="9" y2="15" />
      <line x1="9" y1="9" x2="15" y2="15" />
    </SvgIcon>
  );
}
