defmodule WordCount.CLI do
  @moduledoc false

  def main(args) do
    with {:ok, options} <- parse(args),
         {:ok, bytes} <- File.read(options.path) do
      IO.write(render(bytes, options))
    else
      {:error, reason} when is_binary(reason) ->
        IO.puts(:stderr, "wordcount_elixir: #{reason}")
        System.halt(2)

      {:error, reason} ->
        IO.puts(:stderr, "wordcount_elixir: #{:file.format_error(reason)}")
        System.halt(1)
    end
  end

  defp render(bytes, %{bench_runs: bench_runs} = options) when bench_runs > 0,
    do: render_bench(bytes, options)

  defp render(bytes, options) do
    result = WordCount.count_bytes(bytes, options.top, options.max_word)
    if(options.json, do: render_json(result), else: render_text(result))
  end

  defp parse(args) do
    parse(args, %{
      json: false,
      top: 10,
      max_word: 1024,
      bench_runs: 0,
      bench_warmups: 0,
      path: nil
    })
  end

  defp parse([], %{path: path, top: top} = options)
       when is_binary(path) and top > 0 do
    {:ok, options}
  end

  defp parse(["--json" | rest], options), do: parse(rest, %{options | json: true})
  defp parse(["--top", value | rest], options), do: parse_number(rest, options, :top, value)

  defp parse(["--max-word", value | rest], options),
    do: parse_number(rest, options, :max_word, value)

  defp parse([<<"--top=", value::binary>> | rest], options),
    do: parse_number(rest, options, :top, value)

  defp parse([<<"--max-word=", value::binary>> | rest], options),
    do: parse_number(rest, options, :max_word, value)

  defp parse(["--bench-runs", value | rest], options),
    do: parse_number(rest, options, :bench_runs, value)

  defp parse(["--bench-warmups", value | rest], options),
    do: parse_number(rest, options, :bench_warmups, value)

  defp parse([<<"--bench-runs=", value::binary>> | rest], options),
    do: parse_number(rest, options, :bench_runs, value)

  defp parse([<<"--bench-warmups=", value::binary>> | rest], options),
    do: parse_number(rest, options, :bench_warmups, value)

  defp parse([<<"-", _option::binary>> | _args], _options), do: {:error, usage()}
  defp parse([path | rest], %{path: nil} = options), do: parse(rest, %{options | path: path})
  defp parse(_args, _options), do: {:error, usage()}

  defp parse_number(rest, options, key, value) do
    if whole_decimal?(value) do
      parse(rest, Map.put(options, key, String.to_integer(value)))
    else
      {:error, usage()}
    end
  end

  defp whole_decimal?(value) when byte_size(value) > 0 do
    value
    |> :binary.bin_to_list()
    |> Enum.all?(&(&1 >= ?0 and &1 <= ?9))
  end

  defp whole_decimal?(_value), do: false

  defp usage, do: "usage: wordcount_elixir [--json] [--top N] [--max-word N] <file>"

  defp render_json(result) do
    top =
      result.top
      |> Enum.map_join(",", fn entry -> ~s({"word":"#{entry.word}","count":#{entry.count}}) end)

    ~s({"total":#{result.total},"unique":#{result.unique},"top":[#{top}]}\n)
  end

  defp render_text(result) do
    rows = Enum.map(result.top, fn entry -> "#{entry.count} #{entry.word}" end)

    Enum.join(["count word" | rows] ++ ["total #{result.total}", "unique #{result.unique}"], "\n") <>
      "\n"
  end

  defp render_bench(bytes, options) do
    if options.bench_warmups > 0 do
      Enum.each(1..options.bench_warmups, fn _index ->
        bytes
        |> WordCount.count_bytes(options.top, options.max_word)
        |> checksum()
      end)
    end

    started = System.monotonic_time(:nanosecond)

    checksum =
      Enum.reduce(1..options.bench_runs, 0, fn _index, value ->
        result = WordCount.count_bytes(bytes, options.top, options.max_word)
        Bitwise.bxor(value, checksum(result))
      end)

    mean_ms = (System.monotonic_time(:nanosecond) - started) / 1_000_000 / options.bench_runs
    ~s({"mean_ms":#{:erlang.float_to_binary(mean_ms, decimals: 6)},"checksum":#{checksum}}\n)
  end

  defp checksum(result) do
    Enum.reduce(result.top, Bitwise.bxor(result.total, result.unique), fn entry, value ->
      value
      |> Bitwise.bxor(entry.count)
      |> Bitwise.bxor(byte_size(entry.word))
    end)
  end
end
