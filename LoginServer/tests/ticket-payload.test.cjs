const assert = require("node:assert/strict");
const test = require("node:test");

const {
  buildLoginTicketPayload,
  buildWorldTicketPayload,
} = require("../dist/services/ticket-payload");

test("chat and auction ticket payload contract remains unchanged", () => {
  assert.equal(buildLoginTicketPayload(101, 7, "tester01"), "101:7:7465737465723031");
});

test("world ticket payload binds the target WorldServer instance", () => {
  assert.equal(buildWorldTicketPayload(101, 7, 3, "tester01"), "101:7:3:7465737465723031");
});

test("ticket payload encodes a UTF-8 loginId without delimiter ambiguity", () => {
  assert.equal(buildWorldTicketPayload(9, 2, 1, "안명현"), "9:2:1:ec9588ebaa85ed9884");
});
