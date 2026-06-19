import eslint from "@eslint/js";
import prettier from "eslint-config-prettier/flat";
import globals from "globals";

export default [
  {
    ignores: ["build/**", "coverage/**", "dist/**", "node_modules/**"],
  },
  eslint.configs.recommended,
  {
    languageOptions: {
      ecmaVersion: 2024,
      sourceType: "module",
      globals: {
        ...globals.es2024,
        ...globals.node,
      },
    },
    rules: {
      "array-callback-return": "error",
      eqeqeq: "error",
      "no-debugger": "error",
      "no-duplicate-imports": "error",
      "no-else-return": "error",
      "no-param-reassign": ["error", { props: false }],
      "no-sequences": "error",
      "no-unreachable": "error",
      "no-useless-computed-key": "error",
      "no-useless-escape": "error",
      "no-var": "error",
      "object-shorthand": "error",
      "prefer-const": "error",
      "sort-imports": [
        "error",
        {
          allowSeparatedGroups: true,
          ignoreCase: false,
          ignoreDeclarationSort: true,
          ignoreMemberSort: false,
        },
      ],
      yoda: "error",
    },
  },
  prettier,
];
