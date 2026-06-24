with Interfaces;
with Wordcount_ASCII;

package Wordcount_Checksum
  with SPARK_Mode => On
is
   subtype Checksum is Interfaces.Unsigned_32;
   subtype Count is Interfaces.Unsigned_64;

   Offset : constant Checksum := 2_166_136_261;

   function Mix_Byte
     (Value : Checksum; Byte : Wordcount_ASCII.Byte) return Checksum
   with Global => null, Depends => (Mix_Byte'Result => (Value, Byte));

   function Mix_U32 (Value : Checksum; Item : Checksum) return Checksum
   with Global => null, Depends => (Mix_U32'Result => (Value, Item));

   function Mix_U64 (Value : Checksum; Item : Count) return Checksum
   with Global => null, Depends => (Mix_U64'Result => (Value, Item));
end Wordcount_Checksum;
