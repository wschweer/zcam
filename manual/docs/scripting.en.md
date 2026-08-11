# Scripting

Some properties can be scripted. You can recognise them by the
<img src="../../assets/bound-expression.svg" style="width:25px;"/>
button in the Inspector. A scripted value is calculated using JavaScript.

The button opens an editor in which you can create the script.

After activating the script, the value in the Inspector input field can no
longer be edited — it is highlighted in colour as read-only.

The return value is the value of the specified JavaScript expression.

```
      12                // returns 12
      12*3              // returns 36
      2;3               // returns 3
      [12,24,8]         // returns a vector (e.g. for position)
      {x=12,y=24,z=8}   // alternative syntax for position
```

For properties that consist of multiple values, e.g. Position (Vector3d) or
Size (Vector2d), the script editor lets you specify which component should be
returned. The other component remains editable. For example, you can make the
width (x) of `size` depend on the height (y).

Script for `size` — Component: **All** (Width + Height)

```
      size.y * 0.5            // width is now always half the height
```

## Element References

Elements are referenced in the script by their names. The namespace follows the
project tree:

```
      project.cad.layer1.rectangle4.fill
```

This refers to the `fill` property of an element in the project tree. The path
is composed of `project` followed by the element names along the path in the
project tree.

## Active Switch

The `Active` switch activates the script. When `Active` is off, the property
behaves like an unscripted value and can be freely edited again.

## Default Scripts

The `properties()` JSON can contain a `"script"` field per cell that defines a
default script (analogous to the `"default"` value). An element with a default
script is automatically scriptable (the f(x) button is shown) and the default
script is registered as an active binding when the element is created or when a
project is loaded, provided no manually saved script exists for that property.