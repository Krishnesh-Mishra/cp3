# 4. Enums

```cp3
enum Color { Red, Green, Blue }
let c: Color = Color.Green;

enum Status { Active = "active", Inactive = "inactive" }  // string-backed
```

Both plain (number-backed) and string-backed enums, matching TS. Maps directly to C++'s native `enum class`.

