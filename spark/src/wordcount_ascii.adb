package body Wordcount_ASCII
  with SPARK_Mode => On
is
   function Normalize_Max_Word (Value : Natural) return Word_Limit is
   begin
      if Value = 0 then
         return Default_Max_Word;
      elsif Value < Min_Word then
         return Min_Word;
      elsif Value > Max_Word then
         return Max_Word;
      else
         return Value;
      end if;
   end Normalize_Max_Word;

   function Is_Letter (Value : Byte) return Boolean is
   begin
      return
        (Value >= Character'Pos ('A') and Value <= Character'Pos ('Z'))
        or (Value >= Character'Pos ('a') and Value <= Character'Pos ('z'));
   end Is_Letter;

   function Lower (Value : Byte) return Character is
   begin
      if Value >= Character'Pos ('A') and Value <= Character'Pos ('Z') then
         return Character'Val (Value + 32);
      end if;

      return Character'Val (Value);
   end Lower;
end Wordcount_ASCII;
