{-# LANGUAGE OverloadedRecordDot #-}

module Main (main) where

import Control.Exception (IOException, evaluate, try)
import Control.Monad (foldM, forM_)
import Data.Bits (xor)
import Data.ByteString qualified as ByteString
import Data.ByteString.Char8 qualified as Char8
import Data.Char (isDigit)
import Data.List (intercalate, isPrefixOf, sortBy, stripPrefix)
import Data.Map.Strict qualified as Map
import Data.Word (Word8)
import GHC.Clock (getMonotonicTimeNSec)
import System.Environment (getArgs)
import System.Exit (ExitCode (ExitFailure), exitWith)
import System.IO (hPutStrLn, stderr)
import Text.Printf (printf)
import Text.Read (readMaybe)

data Options = Options
  { json :: !Bool,
    top :: !Int,
    maxWord :: !Int,
    benchRuns :: !Int,
    benchWarmups :: !Int,
    path :: !FilePath
  }

oracleDefaultMaxWord, maxWordLimit, minWord :: Int
oracleDefaultMaxWord = 64
maxWordLimit = 1024
minWord = 4

data Result = Result
  { total :: !Int,
    unique :: !Int,
    topEntries :: ![Entry]
  }

data Entry = Entry
  { word :: !ByteString.ByteString,
    count :: !Int
  }

data Scan = Scan
  { counts :: !(Map.Map ByteString.ByteString Int),
    totalWords :: !Int,
    current :: ![Word8],
    currentLength :: !Int
  }

main :: IO ()
main = do
  parsed <- parseArgs <$> getArgs
  case parsed of
    Left message -> dieWith 2 message
    Right options -> do
      readResult <- try (ByteString.readFile options.path) :: IO (Either IOException ByteString.ByteString)
      case readResult of
        Left readFailure ->
          dieWith 1 ("cannot read " <> options.path <> ": " <> show readFailure)
        Right bytes -> do
          if options.benchRuns > 0
            then renderBench options bytes >>= putStr
            else do
              let result = countBytes options.maxWord options.top bytes
              putStr (if options.json then renderJson result else renderText result)

countBytes :: Int -> Int -> ByteString.ByteString -> Result
{-# NOINLINE countBytes #-}
countBytes maxWord limit bytes =
  let normalizedMaxWord = normalizeMaxWord maxWord
      final = finish (ByteString.foldl' (step normalizedMaxWord) emptyScan bytes)
      entries =
        take limit
          . sortBy compareEntries
          . fmap (uncurry Entry)
          . Map.toList
          $ final.counts
   in Result
        { total = final.totalWords,
          unique = Map.size final.counts,
          topEntries = entries
        }

emptyScan :: Scan
emptyScan =
  Scan
    { counts = Map.empty,
      totalWords = 0,
      current = [],
      currentLength = 0
    }

step :: Int -> Scan -> Word8 -> Scan
step maxWord scan byte
  | isLetter byte =
      if scan.currentLength < maxWord
        then
          scan
            { current = lowerAscii byte : scan.current,
              currentLength = scan.currentLength + 1
            }
        else scan
  | otherwise = finish scan

finish :: Scan -> Scan
finish scan
  | scan.currentLength == 0 = scan
  | otherwise =
      let key = ByteString.pack (reverse scan.current)
       in scan
            { counts = Map.insertWith (+) key 1 scan.counts,
              totalWords = scan.totalWords + 1,
              current = [],
              currentLength = 0
            }

compareEntries :: Entry -> Entry -> Ordering
compareEntries left right =
  compare right.count left.count
    <> compare left.word right.word

isLetter :: Word8 -> Bool
isLetter byte =
  (byte >= upperA && byte <= upperZ)
    || (byte >= lowerA && byte <= lowerZ)

lowerAscii :: Word8 -> Word8
lowerAscii byte
  | byte >= upperA && byte <= upperZ = byte + 32
  | otherwise = byte

upperA, upperZ, lowerA, lowerZ :: Word8
upperA = 65
upperZ = 90
lowerA = 97
lowerZ = 122

parseArgs :: [String] -> Either String Options
parseArgs = go (Options {json = False, top = 10, maxWord = maxWordLimit, benchRuns = 0, benchWarmups = 0, path = ""})
  where
    go options [] =
      if null options.path || options.top <= 0
        then Left usage
        else Right options {maxWord = normalizeMaxWord options.maxWord}
    go options ("--json" : rest) = go options {json = True} rest
    go options ("--top" : value : rest) =
      parseNumber "--top" value >>= \top -> go options {top = top} rest
    go options (arg : rest)
      | Just value <- stripPrefix "--top=" arg =
          parseNumber "--top" value >>= \top -> go options {top = top} rest
    go options ("--max-word" : value : rest) =
      parseNumber "--max-word" value >>= \maxWord ->
        go options {maxWord = maxWord} rest
    go options (arg : rest)
      | Just value <- stripPrefix "--max-word=" arg =
          parseNumber "--max-word" value >>= \maxWord ->
            go options {maxWord = maxWord} rest
    go options ("--bench-runs" : value : rest) =
      parseNumber "--bench-runs" value >>= \benchRuns ->
        go options {benchRuns = benchRuns} rest
    go options (arg : rest)
      | Just value <- stripPrefix "--bench-runs=" arg =
          parseNumber "--bench-runs" value >>= \benchRuns ->
            go options {benchRuns = benchRuns} rest
    go options ("--bench-warmups" : value : rest) =
      parseNumber "--bench-warmups" value >>= \benchWarmups ->
        go options {benchWarmups = benchWarmups} rest
    go options (arg : rest)
      | Just value <- stripPrefix "--bench-warmups=" arg =
          parseNumber "--bench-warmups" value >>= \benchWarmups ->
            go options {benchWarmups = benchWarmups} rest
    go options (arg : rest)
      | "-" `isPrefixOf` arg = Left usage
      | null options.path = go options {path = arg} rest
      | otherwise = Left usage

parseNumber :: String -> String -> Either String Int
parseNumber name value =
  if not (null value) && all isDigit value
    then maybe (Left (name <> " must be a number")) Right (readMaybe value)
    else Left (name <> " must be a number")

normalizeMaxWord :: Int -> Int
normalizeMaxWord 0 = oracleDefaultMaxWord
normalizeMaxWord value = min maxWordLimit (max minWord value)

renderText :: Result -> String
renderText result =
  unlines $
    ["count word"]
      <> fmap renderEntry result.topEntries
      <> ["total " <> show result.total, "unique " <> show result.unique]

renderEntry :: Entry -> String
renderEntry entry = show entry.count <> " " <> Char8.unpack entry.word

renderJson :: Result -> String
renderJson result =
  "{\"total\":"
    <> show result.total
    <> ",\"unique\":"
    <> show result.unique
    <> ",\"top\":["
    <> intercalate "," (fmap renderJsonEntry result.topEntries)
    <> "]}\n"

renderJsonEntry :: Entry -> String
renderJsonEntry entry =
  "{\"word\":\""
    <> Char8.unpack entry.word
    <> "\",\"count\":"
    <> show entry.count
    <> "}"

renderBench :: Options -> ByteString.ByteString -> IO String
renderBench options bytes = do
  forM_ [1 .. options.benchWarmups] $ \index -> do
    input <- varyBytes index bytes
    evaluate (checksum (countBytes options.maxWord options.top input))

  started <- getMonotonicTimeNSec
  checksumValue <-
    foldM
      ( \value index -> do
          input <- varyBytes index bytes
          next <-
            evaluate
              ( checksum
                  (countBytes options.maxWord options.top input)
              )
          pure $! value `xor` next
      )
      0
      [1 .. options.benchRuns]
  finished <- getMonotonicTimeNSec

  let meanMs = fromIntegral (finished - started) / 1_000_000 / fromIntegral options.benchRuns :: Double
  pure (printf "{\"mean_ms\":%.6f,\"checksum\":%d}\n" meanMs checksumValue)

checksum :: Result -> Int
{-# NOINLINE checksum #-}
checksum result =
  foldl'
    (\value entry -> value `xor` entry.count `xor` ByteString.length entry.word)
    (result.total `xor` result.unique)
    result.topEntries

varyBytes :: Int -> ByteString.ByteString -> IO ByteString.ByteString
{-# NOINLINE varyBytes #-}
varyBytes index bytes = evaluate index >> pure bytes

dieWith :: Int -> String -> IO ()
dieWith code message = do
  hPutStrLn stderr ("wordcount_haskell: " <> message)
  exitWith (ExitFailure code)

usage :: String
usage = "usage: wordcount_haskell [--json] [--top N] [--max-word N] <file>"
