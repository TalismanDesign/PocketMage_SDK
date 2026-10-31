export default {
  title: "PocketMage SDK",
  url: "https://talismandesign.github.io/PocketMage_SDK/docs",
  logo: { alt: "PocketMage SDK", href: "./" },
  favicon: "/assets/favicon.png",
  theme: {
    name: "ruby",
    defaultMode: "system",
    enableModeToggle: true,
    positionMode: "top",
    codeHighlight: true,
    customCss: ["/assets/css/theme.css"],
    copyWidgets: {
      enabled: true,
      raw: true,
      context: true,
    },
  },
  layout: {
    footer: {
      style: "complete",
      description: "The official SDK for PocketMage apps: each app is an ELF the OS loads and runs in-process.",
      branding: true,
      columns: [
        {
          title: "Get Started",
          links: [
            { text: "Overview", url: "./get-started/" },
            { text: "Install the toolchain", url: "./get-started/toolchain/" },
            { text: "Your first app", url: "./get-started/first-app/" },
          ],
        },
        {
          title: "Reference",
          links: [
            { text: "Function index", url: "./api/functions/" },
            { text: "App ABI", url: "./app-abi/" },
            { text: "Overlay format", url: "./overlay/" },
            { text: "Symbol surface", url: "./symbols/" },
          ],
        },
        {
          title: "Tooling",
          links: [
            { text: "Building apps", url: "./build/" },
            { text: "pm CLI", url: "./cli/" },
            { text: "Publishing", url: "./guides/publishing/" },
          ],
        },
        {
          title: "Community",
          links: [
            { text: "PocketMageOS docs", url: "https://talismandesign.github.io/PocketMage_PDA/docs" },
            { text: "GitHub", url: "https://github.com/TalismanDesign/PocketMage_SDK" },
            { text: "Discord", url: "https://discord.gg/KSCapSf4XH" },
          ],
        },
      ],
    },
  },
  plugins: {
    search: {
      semantic: false,
    },
    seo: {
      defaultDescription:
        "Official SDK for building PocketMage apps: the ELF app contract, build tooling, symbol surface, and publishing flow.",
      twitter: { cardType: "summary" },
    },
    sitemap: {
      defaultChangefreq: "weekly",
      defaultPriority: 0.8,
    },
    mermaid: {},
    git: {},
    llms: {
      fullContext: true,
    },
  },
  search: true,
  minify: true,
  autoTitleFromH1: true,
  copyCode: true,
  pageNavigation: true,
  navigation: [
    { title: "Home", path: "/", icon: "home" },
    {
      title: "Get Started",
      icon: "rocket",
      collapsible: true,
      path: "/get-started",
      children: [
        { title: "Install the toolchain", path: "/get-started/toolchain", icon: "settings" },
        { title: "Your first app", path: "/get-started/first-app", icon: "play" },
      ],
    },
    {
      title: "Guides",
      icon: "book",
      collapsible: true,
      path: "/guides",
      children: [
        { title: "Making an app", path: "/guides/making-apps", icon: "code" },
        { title: "App lifecycle", path: "/guides/lifecycle", icon: "activity" },
        { title: "Rendering", path: "/guides/rendering", icon: "monitor" },
        { title: "Internationalization", path: "/guides/i18n", icon: "languages" },
        { title: "Publishing an app", path: "/guides/publishing", icon: "package" },
      ],
    },
    {
      title: "API Reference",
      icon: "boxes",
      collapsible: true,
      path: "/api",
      children: [
        { title: "Function index", path: "/api/functions", icon: "list" },
        { title: "Configuration", path: "/api/configuration", icon: "sliders" },
        { title: "OLED", path: "/api/oled", icon: "monitor" },
        { title: "E-Ink", path: "/api/eink", icon: "monitor" },
        { title: "Fonts & text", path: "/api/font", icon: "text" },
        { title: "Layout", path: "/api/layout", icon: "ruler" },
        { title: "UI helpers", path: "/api/ui", icon: "layout-grid" },
        { title: "SD card", path: "/api/sd", icon: "hard-drive" },
        { title: "Keyboard", path: "/api/kb", icon: "keyboard" },
        { title: "Touch", path: "/api/touch", icon: "hand" },
        { title: "Buzzer", path: "/api/bz", icon: "volume" },
        { title: "Clock", path: "/api/clock", icon: "clock" },
        { title: "WiFi", path: "/api/wifi", icon: "wifi" },
        { title: "System", path: "/api/sys", icon: "cpu" },
        { title: "i18n (runtime)", path: "/api/i18n-runtime", icon: "languages" },
        { title: "Text utilities", path: "/api/io", icon: "type" },
        { title: "Frames", path: "/api/frames", icon: "frame" },
        { title: "Built-in assets", path: "/api/assets", icon: "image" },
      ],
    },
    {
      title: "Binary Contract",
      icon: "file-code",
      collapsible: true,
      children: [
        { title: "App ABI", path: "/app-abi", icon: "book" },
        { title: "Overlay format (.elfo)", path: "/overlay", icon: "layers" },
        { title: "Symbol surface", path: "/symbols", icon: "box" },
      ],
    },
    {
      title: "Tooling",
      icon: "wrench",
      collapsible: true,
      children: [
        { title: "Building apps", path: "/build", icon: "code" },
        { title: "pm CLI", path: "/cli", icon: "terminal" },
        { title: "i18n catalogs", path: "/i18n", icon: "languages" },
      ],
    },
    { title: "Troubleshooting", path: "/troubleshooting", icon: "life-buoy" },
    { title: "Glossary", path: "/glossary", icon: "book-open" },
    { title: "Migration", path: "/migration", icon: "git-branch" },
    {
      title: "PocketMageOS docs",
      path: "https://talismandesign.github.io/PocketMage_PDA/docs",
      icon: "cpu",
      external: true,
    },
    {
      title: "GitHub",
      path: "https://github.com/TalismanDesign/PocketMage_SDK",
      icon: "github",
      external: true,
    },
  ],
  footer: "Built with [docmd](https://docmd.io). [View on GitHub](https://github.com/TalismanDesign/PocketMage_SDK).",
  editLink: {
    enabled: true,
    baseUrl: "https://github.com/TalismanDesign/PocketMage_SDK/edit/main/Docs/docs",
    text: "Edit this page",
  },
};