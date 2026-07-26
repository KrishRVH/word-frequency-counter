%{
  configs: [
    %{
      name: "default",
      strict: true,
      files: %{
        included: ["lib/", "mix.exs"],
        excluded: ["_build/", "deps/"]
      },
      checks: [
        {Credo.Check.Refactor.CyclomaticComplexity, max_complexity: 10},
        {Credo.Check.Refactor.Nesting, max_nesting: 3},
        {Credo.Check.Readability.MaxLineLength, max_length: 120}
      ]
    }
  ]
}
