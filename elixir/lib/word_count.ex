defmodule WordCount do
  @moduledoc false

  @default_max_word 64
  @max_word 1024
  @min_word 4

  def count_bytes(bytes, top, max_word) when is_binary(bytes) do
    max_word = normalize_max_word(max_word)

    {counts, total, word, _stored} =
      for <<byte <- bytes>>, reduce: {%{}, 0, [], 0} do
        state -> step(byte, state, max_word)
      end

    {counts, total} = finish_word(counts, total, word)

    entries =
      counts
      |> Enum.map(fn {word, count} -> %{word: word, count: count} end)
      |> Enum.sort_by(fn entry -> {-entry.count, entry.word} end)
      |> Enum.take(top)

    %{total: total, unique: map_size(counts), top: entries}
  end

  defp step(byte, {counts, total, word, stored}, max_word) do
    cond do
      letter?(byte) and stored < max_word ->
        {counts, total, [lower_ascii(byte) | word], stored + 1}

      letter?(byte) ->
        {counts, total, word, stored}

      word != [] ->
        {counts, total} = finish_word(counts, total, word)
        {counts, total, [], 0}

      true ->
        {counts, total, word, stored}
    end
  end

  defp finish_word(counts, total, []), do: {counts, total}

  defp finish_word(counts, total, word) do
    key = word |> Enum.reverse() |> List.to_string()
    {Map.update(counts, key, 1, &(&1 + 1)), total + 1}
  end

  defp letter?(byte) do
    lower = Bitwise.bor(byte, 32)
    lower >= ?a and lower <= ?z
  end

  defp lower_ascii(byte) when byte >= ?A and byte <= ?Z, do: byte + 32
  defp lower_ascii(byte), do: byte

  defp normalize_max_word(0), do: @default_max_word
  defp normalize_max_word(max_word) when max_word < @min_word, do: @min_word
  defp normalize_max_word(max_word) when max_word > @max_word, do: @max_word
  defp normalize_max_word(max_word), do: max_word
end
