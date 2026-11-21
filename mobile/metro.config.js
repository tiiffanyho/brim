// Lets the native app reuse code that lives outside mobile/:
//   @schema  -> shared/gen/events.ts   (generated event + API types, single source of truth)
//   @web/*   -> app/src/*              (the web app's pure modules: describe, speech, ws)
// Expo's tsconfig "paths" support does not reach outside the project root, so map them here.
const path = require("path");
const { getDefaultConfig } = require("expo/metro-config");

const repoRoot = path.resolve(__dirname, "..");
const config = getDefaultConfig(__dirname);

config.watchFolders = [path.join(repoRoot, "shared", "gen"), path.join(repoRoot, "app", "src")];

const aliases = {
  "@schema": path.join(repoRoot, "shared", "gen", "events.ts"),
};
const webPrefix = "@web/";
const webRoot = path.join(repoRoot, "app", "src");

const upstream = config.resolver.resolveRequest;
config.resolver.resolveRequest = (context, moduleName, platform) => {
  let target = null;
  if (aliases[moduleName]) target = aliases[moduleName];
  else if (moduleName.startsWith(webPrefix)) target = path.join(webRoot, moduleName.slice(webPrefix.length));
  if (target) return context.resolveRequest(context, target, platform);
  return upstream ? upstream(context, moduleName, platform) : context.resolveRequest(context, moduleName, platform);
};

module.exports = config;
