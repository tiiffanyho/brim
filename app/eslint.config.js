import js from "@eslint/js";
import tseslint from "typescript-eslint";
import reactHooks from "eslint-plugin-react-hooks";
import jsxA11y from "eslint-plugin-jsx-a11y";
import globals from "globals";

export default tseslint.config(
  { ignores: ["dist", "node_modules", "public/sw.js"] },
  {
    files: ["**/*.{ts,tsx}"],
    extends: [js.configs.recommended, ...tseslint.configs.recommended, jsxA11y.flatConfigs.strict],
    languageOptions: { globals: globals.browser },
    plugins: { "react-hooks": reactHooks },
    rules: {
      ...reactHooks.configs.recommended.rules,
      // Screen-reader users: autofocus is managed explicitly via refs on route change.
      "jsx-a11y/no-autofocus": "error",
    },
  },
);
