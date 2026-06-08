# Syntax highlighting and language configuration fixture.

@0xdba53d6c0e9fe302;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("capnp_ls_test");

annotation deprecated(field, method): Text;

const defaultName: Text = "Ada";

enum Role {
  engineer @0;
  manager @1;
}

struct Person {
  id @0: UInt64;
  name @1: Text = .defaultName;
  role @2: Role;
  tags @3: List(Text);
  oldName @4: Text $deprecated("Use name instead");

  contact: union {
    email @5: Text;
    phone @6: Text;
  }
}
