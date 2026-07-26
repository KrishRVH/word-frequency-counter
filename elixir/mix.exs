defmodule WordCount.MixProject do
  use Mix.Project

  def project do
    [
      app: :word_count,
      version: "0.1.0",
      elixir: "~> 1.20",
      start_permanent: Mix.env() == :prod,
      escript: [main_module: WordCount.CLI],
      dialyzer: [
        flags: [
          :error_handling,
          :extra_return,
          :missing_return,
          :underspecs,
          :unknown,
          :unmatched_returns
        ]
      ],
      deps: deps()
    ]
  end

  def application do
    [extra_applications: [:logger]]
  end

  defp deps do
    [
      {:credo, "~> 1.7.19", only: [:dev], runtime: false},
      {:dialyxir, "~> 1.4", only: [:dev], runtime: false},
      {:ex_doc, "~> 0.40", only: [:dev], runtime: false},
      {:mix_audit, "~> 2.1", only: [:dev], runtime: false}
    ]
  end
end
