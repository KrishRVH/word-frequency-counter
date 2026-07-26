import path from 'node:path';
import { fileURLToPath } from 'node:url';

import eslint from '@eslint/js';
import { defineConfig, globalIgnores } from 'eslint/config';
import prettier from 'eslint-config-prettier/flat';
import regexp from 'eslint-plugin-regexp';
import globals from 'globals';
import tseslint from 'typescript-eslint';

const __dirname = path.dirname(fileURLToPath(import.meta.url));

// eslint-disable-next-line no-restricted-exports
export default defineConfig(
  globalIgnores(
    ['**/build/**', '**/coverage/**', '**/dist/**', '**/node_modules/**', '**/out/**', '**/*.tsbuildinfo'],
    'base/global-ignores',
  ),
  { name: 'base/eslint/recommended', ...eslint.configs.recommended },
  { ...regexp.configs['flat/recommended'], name: 'regexp/recommended' },
  {
    name: 'base/language',
    languageOptions: {
      ecmaVersion: 2024,
      sourceType: 'module',
      globals: {
        ...globals.es2024,
      },
    },
  },
  {
    name: 'typescript/parse-only',
    files: ['**/*.{ts,tsx}'],
    languageOptions: {
      parser: tseslint.parser,
      parserOptions: {
        ecmaFeatures: { jsx: true },
        ecmaVersion: 2024,
        sourceType: 'module',
      },
    },
  },
  {
    name: 'imports/baseline',
    files: ['**/*.{js,jsx,ts,tsx,mjs,cjs}'],
    rules: {
      'no-duplicate-imports': 'error',
      'no-restricted-exports': [
        'error',
        {
          restrictDefaultExports: {
            direct: true,
            named: true,
            defaultFrom: true,
            namedFrom: true,
            namespaceFrom: true,
          },
        },
      ],
      'sort-imports': [
        'error',
        {
          ignoreCase: false,
          ignoreDeclarationSort: true,
          ignoreMemberSort: false,
          allowSeparatedGroups: true,
        },
      ],
    },
  },
  {
    name: 'typescript/strict-typechecked',
    files: ['src/**/*.{ts,tsx}'],
    ignores: ['**/*.d.ts'],
    extends: [...tseslint.configs.strictTypeChecked, ...tseslint.configs.stylisticTypeChecked],
    languageOptions: {
      parserOptions: {
        projectService: true,
        tsconfigRootDir: __dirname,
      },
    },
    rules: {
      'no-restricted-syntax': [
        'error',
        {
          selector: 'TSEnumDeclaration',
          message: 'Do not use TypeScript enums. Use as const objects + union types.',
        },
        {
          selector: 'TSModuleDeclaration',
          message: 'Ambient declarations (declare global/module/namespace) must live in *.d.ts files only.',
        },
        {
          selector: 'TSParameterProperty',
          message: 'Do not use parameter properties (constructor(public x: number)). Declare fields explicitly.',
        },
        { selector: 'TSImportEqualsDeclaration', message: 'Do not use import =. Use standard ES imports.' },
        { selector: 'TSExportAssignment', message: 'Do not use export =. Use ES exports.' },
      ],
      'array-callback-return': 'error',
      eqeqeq: 'error',
      'no-debugger': 'error',
      'no-else-return': 'error',
      'no-param-reassign': ['error', { props: false }],
      'no-sequences': 'error',
      'no-unreachable': 'error',
      'no-useless-computed-key': 'error',
      'no-useless-escape': 'error',
      'no-useless-return': 'error',
      'no-var': 'error',
      'object-shorthand': 'error',
      'prefer-const': 'error',
      yoda: 'error',
      '@typescript-eslint/consistent-type-exports': ['error', { fixMixedExportsWithInlineTypeSpecifier: true }],
      '@typescript-eslint/consistent-type-imports': [
        'error',
        { prefer: 'type-imports', fixStyle: 'separate-type-imports' },
      ],
      '@typescript-eslint/no-confusing-void-expression': ['error', { ignoreArrowShorthand: true }],
      '@typescript-eslint/no-empty-object-type': 'error',
      '@typescript-eslint/no-explicit-any': 'error',
      '@typescript-eslint/no-floating-promises': ['error', { ignoreVoid: true }],
      '@typescript-eslint/no-import-type-side-effects': 'error',
      '@typescript-eslint/no-require-imports': 'error',
      '@typescript-eslint/no-unnecessary-condition': 'error',
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_', varsIgnorePattern: '^_' }],
      '@typescript-eslint/no-use-before-define': 'error',
      '@typescript-eslint/no-wrapper-object-types': 'error',
      '@typescript-eslint/prefer-includes': 'error',
      '@typescript-eslint/prefer-readonly': 'error',
      '@typescript-eslint/restrict-template-expressions': ['error', { allowNumber: true }],
      '@typescript-eslint/ban-ts-comment': [
        'error',
        {
          'ts-expect-error': 'allow-with-description',
          'ts-ignore': true,
          'ts-nocheck': true,
          'ts-check': true,
          minimumDescriptionLength: 10,
        },
      ],
      '@typescript-eslint/no-non-null-assertion': 'error',
      '@typescript-eslint/only-throw-error': 'error',
      '@typescript-eslint/switch-exhaustiveness-check': 'error',
      '@typescript-eslint/strict-boolean-expressions': [
        'error',
        {
          allowString: false,
          allowNumber: false,
          allowNullableObject: false,
          allowNullableBoolean: false,
          allowNullableString: false,
          allowNullableNumber: false,
          allowAny: false,
        },
      ],
    },
  },
  {
    name: 'typescript/dts-ambient-ok',
    files: ['**/*.d.ts'],
    rules: {
      'no-restricted-syntax': [
        'error',
        { selector: 'TSEnumDeclaration', message: 'Do not use TypeScript enums. Use as const objects + union types.' },
        { selector: 'TSParameterProperty', message: 'No parameter properties.' },
        { selector: 'TSImportEqualsDeclaration', message: 'No import =.' },
        { selector: 'TSExportAssignment', message: 'No export =.' },
      ],
    },
  },
  {
    name: 'javascript/esm-only',
    files: ['**/*.{js,mjs,jsx}'],
    extends: [tseslint.configs.disableTypeChecked],
    rules: {
      'no-restricted-syntax': [
        'error',
        { selector: "CallExpression[callee.name='require']", message: 'Do not use require(). Use ESM imports.' },
        {
          selector: "MemberExpression[object.name='module'][property.name='exports']",
          message: 'Do not use module.exports. Use ESM exports.',
        },
        { selector: "MemberExpression[object.name='exports']", message: 'Do not use exports.*. Use ESM exports.' },
      ],
    },
  },
  { ...prettier, name: 'prettier/config' },
  { name: 'base/prettier-overrides', rules: { curly: 'error' } },
  { name: 'base/hygiene', linterOptions: { reportUnusedDisableDirectives: 'error' } },
);
