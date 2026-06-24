package Wordcount_ASCII
  with SPARK_Mode => On
is
   subtype Byte is Natural range 0 .. 255;
   subtype Word_Limit is Positive range 4 .. 1_024;

   Default_Max_Word : constant Word_Limit := 64;
   Min_Word         : constant Word_Limit := 4;
   Max_Word         : constant Word_Limit := 1_024;

   function Normalize_Max_Word (Value : Natural) return Word_Limit
   with
     Global  => null,
     Depends => (Normalize_Max_Word'Result => Value),
     Post    =>
       (if Value = 0
        then Normalize_Max_Word'Result = Default_Max_Word
        elsif Value < Min_Word
        then Normalize_Max_Word'Result = Min_Word
        elsif Value > Max_Word
        then Normalize_Max_Word'Result = Max_Word
        else Normalize_Max_Word'Result = Value);

   function Is_Letter (Value : Byte) return Boolean
   with
     Global  => null,
     Depends => (Is_Letter'Result => Value),
     Post    =>
       Is_Letter'Result
       = ((Value >= Character'Pos ('A') and Value <= Character'Pos ('Z'))
          or (Value >= Character'Pos ('a') and Value <= Character'Pos ('z')));

   function Lower (Value : Byte) return Character
   with
     Global  => null,
     Depends => (Lower'Result => Value),
     Pre     => Is_Letter (Value),
     Post    =>
       (if Value >= Character'Pos ('A') and Value <= Character'Pos ('Z')
        then Character'Pos (Lower'Result) = Value + 32
        else Character'Pos (Lower'Result) = Value);
end Wordcount_ASCII;
