using System.Collections.Generic;

namespace WordFrequencyCounter;

internal sealed record Result(ulong Total, int Unique, IReadOnlyList<Entry> Top);
