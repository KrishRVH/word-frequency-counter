package body Wordcount_Checksum
  with SPARK_Mode => On
is
   use type Checksum;
   use type Count;

   Prime : constant Checksum := 16_777_619;

   function Mix_Byte
     (Value : Checksum; Byte : Wordcount_ASCII.Byte) return Checksum is
   begin
      return (Value xor Checksum (Byte)) * Prime;
   end Mix_Byte;

   function Mix_U32 (Value : Checksum; Item : Checksum) return Checksum is
      Mixed     : Checksum := Value;
      Remaining : Checksum := Item;
   begin
      for Index in 1 .. 4 loop
         Mixed := Mix_Byte (Mixed, Wordcount_ASCII.Byte (Remaining mod 256));
         Remaining := Remaining / 256;
      end loop;

      return Mixed;
   end Mix_U32;

   function Mix_U64 (Value : Checksum; Item : Count) return Checksum is
      Mixed     : Checksum := Value;
      Remaining : Count := Item;
   begin
      for Index in 1 .. 8 loop
         Mixed := Mix_Byte (Mixed, Wordcount_ASCII.Byte (Remaining mod 256));
         Remaining := Remaining / 256;
      end loop;

      return Mixed;
   end Mix_U64;
end Wordcount_Checksum;
