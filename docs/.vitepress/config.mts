import { defineConfig } from 'vitepress'
import { withMermaid } from 'vitepress-plugin-mermaid'

const repo = 'https://github.com/Zaydme/openaok'

export default withMermaid(defineConfig({
  title: 'OpenAoK',
  description: 'A free, open-source engine for Age of Empires II: The Age of Kings, played from your own CD',
  base: '/openaok/',
  cleanUrls: true,
  lastUpdated: false,
  srcExclude: ['**/node_modules/**', 'README.md'],
  head: [['link', { rel: 'icon', type: 'image/svg+xml', href: '/openaok/images/key.svg' }]],
  vite: { build: { chunkSizeWarningLimit: 4000 } },
  themeConfig: {
    logo: '/images/key.svg',
    nav: [
      { text: 'Guide', link: '/guide/', activeMatch: '/guide/' },
      { text: 'Engine', link: '/engine/', activeMatch: '/engine/' },
      { text: 'Development', link: '/development/', activeMatch: '/development/' },
      { text: 'Download', link: `${repo}/releases` },
    ],
    sidebar: {
      '/guide/': [
        {
          text: 'Getting started',
          items: [
            { text: 'Introduction', link: '/guide/' },
            { text: 'Your game files', link: '/guide/game-files' },
          ],
        },
        {
          text: 'Installing',
          items: [
            { text: 'Windows', link: '/guide/windows' },
            { text: 'macOS and Linux', link: '/guide/desktop' },
            { text: 'Android', link: '/guide/android' },
            { text: 'iPhone and iPad', link: '/guide/ios' },
            { text: 'Nintendo Switch', link: '/guide/switch' },
          ],
        },
        {
          text: 'Playing',
          items: [
            { text: 'Controls', link: '/guide/controls' },
            { text: 'Options', link: '/guide/options' },
            { text: 'Improvements on the original', link: '/guide/improvements' },
            { text: 'Multiplayer', link: '/guide/multiplayer' },
            { text: 'Command line', link: '/guide/command-line' },
            { text: 'Troubleshooting', link: '/guide/troubleshooting' },
          ],
        },
      ],
      '/engine/': [
        {
          text: 'The engine',
          items: [
            { text: 'Overview', link: '/engine/' },
            { text: 'The game\'s data', link: '/engine/data' },
            { text: 'Random maps', link: '/engine/random-maps' },
          ],
        },
        {
          text: 'Simulation',
          items: [
            { text: 'Entities and systems', link: '/engine/simulation' },
            { text: 'Fixed point and determinism', link: '/engine/determinism' },
            { text: 'Movement and routes', link: '/engine/pathfinding' },
            { text: 'Combat', link: '/engine/combat' },
            { text: 'Computer players', link: '/engine/ai' },
            { text: 'Lockstep multiplayer', link: '/engine/multiplayer' },
          ],
        },
        {
          text: 'Rendering',
          items: [
            { text: 'The frame', link: '/engine/rendering/' },
            { text: 'Sprites and texture pages', link: '/engine/rendering/sprites' },
            { text: 'Terrain', link: '/engine/rendering/terrain' },
            { text: 'Fog of war', link: '/engine/rendering/fog' },
          ],
        },
        {
          text: 'Around the game',
          items: [
            { text: 'The interface', link: '/engine/interface' },
            { text: 'Sound, music and movies', link: '/engine/audio' },
            { text: 'Platforms', link: '/engine/platforms' },
          ],
        },
      ],
      '/development/': [
        {
          text: 'Development',
          items: [
            { text: 'Building from source', link: '/development/' },
            { text: 'Self-checks', link: '/development/verification' },
            { text: 'Performance', link: '/development/performance' },
            { text: 'Contributing', link: '/development/contributing' },
          ],
        },
      ],
    },
    socialLinks: [{ icon: 'github', link: repo }],
    editLink: { pattern: `${repo}/edit/main/docs/:path`, text: 'Edit this page on GitHub' },
    search: { provider: 'local' },
    outline: { level: [2, 3] },
    footer: {
      message: 'Released under the MIT license. Not affiliated with, endorsed by or sponsored by Microsoft.',
      copyright: 'Age of Empires II: The Age of Kings © Microsoft Corporation.',
    },
  },
  // A font every system has at once, so labels are measured in the face
  // they are drawn in.
  mermaid: {
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif',
    themeVariables: {
      fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif',
    },
  },
}))
