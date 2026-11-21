// The iPhone app is self-contained except for one thing it shares with the hat and the website:
//   @schema  -> shared/gen/events.ts   (generated event + API types, single source of truth)
// Expo's tsconfig "paths" support does not reach outside the project root, so map it here.
// The app never imports from app/ (the website): it has its own copies in src/core/.
const path = require("path");
const { getDefaultConfig } = require("expo/metro-config");

const repoRoot = path.resolve(__dirname, "..");
const schema = path.join(repoRoot, "shared", "gen", "events.ts");
const config = getDefaultConfig(__dirname);

config.watchFolders = [path.dirname(schema)];

const upstream = config.resolver.resolveRequest;
config.resolver.resolveRequest = (context, moduleName, platform) => {
  if (moduleName === "@schema") return context.resolveRequest(context, schema, platform);
  return upstream ? upstream(context, moduleName, platform) : context.resolveRequest(context, moduleName, platform);
};

module.exports = config;
