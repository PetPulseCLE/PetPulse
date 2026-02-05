import { useState, useEffect } from "react";

export function SplashScreen({ fadeOut }: { fadeOut: boolean }) {
  const [showDog, setShowDog] = useState(true);

  useEffect(() => {
    // Alternate between dog and cat every 1 second
    const interval = setInterval(() => {
      setShowDog((prev) => !prev);
    }, 1000);

    return () => clearInterval(interval);
  }, []);

  return (
    <div
      className={`min-h-screen bg-background flex flex-col items-center justify-center p-6 transition-opacity duration-500 ${fadeOut ? "opacity-0" : "opacity-100 animate-in fade-in"}`}
    >
      <div className="text-center space-y-6">
        {/* Dog / Cat Logo */}
        <div className="flex justify-center mb-8">
          <div className="relative w-32 h-32">
            {/* Circle background */}
            <div className="absolute inset-0 rounded-full bg-primary"></div>

            {/* Dog Face */}
            <svg
              viewBox="0 0 100 100"
              className={`absolute inset-0 w-full h-full transition-opacity duration-500 ${showDog ? "opacity-100" : "opacity-0"}`}
            >
              {/* Left ear - Floppy */}
              <ellipse
                cx="25"
                cy="35"
                rx="8"
                ry="15"
                fill="white"
                opacity="0.9"
              />

              {/* Right ear - Floppy */}
              <ellipse
                cx="75"
                cy="35"
                rx="8"
                ry="15"
                fill="white"
                opacity="0.9"
              />

              {/* Face circle */}
              <circle cx="50" cy="50" r="25" fill="white" opacity="0.95" />

              {/* Left eye - Round */}
              <circle cx="42" cy="45" r="4" fill="#030213" />

              {/* Right eye - Round */}
              <circle cx="58" cy="45" r="4" fill="#030213" />

              {/* Nose */}
              <ellipse cx="50" cy="55" rx="4" ry="3" fill="#030213" />

              {/* Mouth */}
              <path
                d="M 50 55 L 50 60"
                stroke="#030213"
                strokeWidth="2"
                strokeLinecap="round"
              />
              <path
                d="M 50 60 Q 45 63 42 61"
                stroke="#030213"
                strokeWidth="1.5"
                fill="none"
                strokeLinecap="round"
              />
              <path
                d="M 50 60 Q 55 63 58 61"
                stroke="#030213"
                strokeWidth="1.5"
                fill="none"
                strokeLinecap="round"
              />

              {/* Tongue */}
              <ellipse
                cx="50"
                cy="64"
                rx="3"
                ry="2"
                fill="#ff6b9d"
                opacity="0.8"
              />
            </svg>

            {/* Cat Face */}
            <svg
              viewBox="0 0 100 100"
              className={`absolute inset-0 w-full h-full transition-opacity duration-500 ${showDog ? "opacity-0" : "opacity-100"}`}
            >
              {/* Left ear - Pointed */}
              <path d="M 25 30 L 18 15 L 30 25 Z" fill="white" opacity="0.9" />

              {/* Right ear - Pointed */}
              <path d="M 75 30 L 82 15 L 70 25 Z" fill="white" opacity="0.9" />

              {/* Face circle */}
              <circle cx="50" cy="50" r="25" fill="white" opacity="0.95" />

              {/* Left eye - Almond/Slanted */}
              <ellipse
                cx="42"
                cy="46"
                rx="3"
                ry="5"
                fill="#030213"
                transform="rotate(-15 42 46)"
              />

              {/* Right eye - Almond/Slanted */}
              <ellipse
                cx="58"
                cy="46"
                rx="3"
                ry="5"
                fill="#030213"
                transform="rotate(15 58 46)"
              />

              {/* Nose */}
              <path d="M 50 52 L 47 56 L 53 56 Z" fill="#030213" />

              {/* Mouth */}
              <path
                d="M 50 56 Q 45 59 42 57"
                stroke="#030213"
                strokeWidth="1.5"
                fill="none"
                strokeLinecap="round"
              />
              <path
                d="M 50 56 Q 55 59 58 57"
                stroke="#030213"
                strokeWidth="1.5"
                fill="none"
                strokeLinecap="round"
              />

              {/* Whiskers - Left */}
              <line
                x1="25"
                y1="50"
                x2="35"
                y2="48"
                stroke="white"
                strokeWidth="1.5"
                opacity="0.8"
              />
              <line
                x1="25"
                y1="53"
                x2="35"
                y2="53"
                stroke="white"
                strokeWidth="1.5"
                opacity="0.8"
              />

              {/* Whiskers - Right */}
              <line
                x1="75"
                y1="50"
                x2="65"
                y2="48"
                stroke="white"
                strokeWidth="1.5"
                opacity="0.8"
              />
              <line
                x1="75"
                y1="53"
                x2="65"
                y2="53"
                stroke="white"
                strokeWidth="1.5"
                opacity="0.8"
              />
            </svg>
          </div>
        </div>

        {/* App Name */}
        <div className="space-y-2">
          <h1 className="text-4xl">Pet Pulse</h1>
          <p className="text-muted-foreground">Care Beyond the Collar</p>
        </div>
      </div>
    </div>
  );
}
