
print("CTEST_FULL_OUTPUT")
val, message = pcall(function()
	osg.PositionAttitudeTransform().getPosition()
end)

print("val = " .. tostring(val))
print("message = " .. tostring(message))
print("Done!")
