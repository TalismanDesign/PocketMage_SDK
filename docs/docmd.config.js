export default {
  title: "PocketMage SDK",
  url: "https://talismandesign.github.io/PocketMage_SDK/docs",
  logo: { alt: "PocketMage SDK", href: "./" },
  theme: {
    name: "ruby",
    defaultMode: "system",
    enableModeToggle: true,
    positionMode: "top",
    codeHighlight: true,
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
          title: "Guides",
          links: [
            { text: "Making an app", url: "./guides/making-apps/" },
            { text: "App lifecycle", url: "./guides/lifecycle/" },
            { text: "Rendering", url: "./guides/rendering/" },
          ],
        },
        {
          title: "API Reference",
          links: [
            { text: "Overview", url: "./api/" },
            { text: "OLED", url: "./api/oled/" },
            { text: "E-Ink", url: "./api/eink/" },
            { text: "System", url: "./api/sys/" },
          ],
        },
        {
          title: "Platform",
          links: [
            { text: "App ABI", url: "./app-abi/" },
            { text: "Building Apps", url: "./build/" },
            { text: "Symbols", url: "./symbols/" },
            { text: "Publishing", url: "./publish/" },
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
      title: "Guides",
      icon: "book",
      collapsible: true,
      children: [
        { title: "Guide index", path: "/guides", icon: "book" },
        { title: "Making an app", path: "/guides/making-apps", icon: "code" },
        { title: "App lifecycle", path: "/guides/lifecycle", icon: "activity" },
        { title: "Rendering", path: "/guides/rendering", icon: "monitor" },
      ],
    },
    {
      title: "API Reference",
      icon: "boxes",
      collapsible: true,
      children: [
        { title: "API overview", path: "/api", icon: "boxes" },
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
    { title: "App ABI", path: "/app-abi", icon: "book" },
    { title: "Building Apps", path: "/build", icon: "code" },
    { title: "Symbols", path: "/symbols", icon: "box" },
    { title: "Publishing", path: "/publish", icon: "package" },
    { title: "i18n catalogs", path: "/i18n", icon: "terminal" },
    { title: "Migration", path: "/migration", icon: "book-open" },
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