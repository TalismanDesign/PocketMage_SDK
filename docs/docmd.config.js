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
          title: "Documentation",
          links: [
            { text: "App ABI", url: "./app-abi/" },
            { text: "Building Apps", url: "./build/" },
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
    { title: "App ABI", path: "/app-abi", icon: "book" },
    { title: "Building Apps", path: "/build", icon: "code" },
    { title: "Symbols", path: "/symbols", icon: "box" },
    { title: "Publishing", path: "/publish", icon: "package" },
    { title: "i18n", path: "/i18n", icon: "terminal" },
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