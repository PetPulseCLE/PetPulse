# PetPulse email confirmation redirect

Static page that redirects from an **https** link (so Gmail/forwarding don't strip it) to the app deep link `petpulse://auth/callback?token_hash=...&type=signup`.

## Deploy to Cloudflare Pages (free) — Direct Upload

1. **Create a zip of this folder.** Zip the **contents** of `email-redirect` so the zip has `auth/` and `README.md` at the top (not a folder named `email-redirect` inside the zip). On macOS: open `email-redirect`, select `auth` and `README.md`, right-click → **Compress** (or in Terminal: `cd email-redirect && zip -r ../email-redirect.zip .`).

2. **In Cloudflare Dashboard:** [Pages](https://dash.cloudflare.com/) → **Workers & Pages** → **Create** → **Pages** → choose **Direct Upload** (not Connect to Git).

3. **Upload:** **Project name** e.g. `petpulse-email-redirect`, then drag and drop your zip (or click to select it). Click **Deploy.**

4. **Note your URL** after deploy, e.g. `https://petpulse-email-redirect.pages.dev`. Your redirect URL is: **`https://<your-project>.pages.dev/auth/confirm`**

## Supabase setup

1. **Redirect URLs** (Authentication → URL Configuration):  
   Add: `https://your-project.pages.dev/auth/confirm`  
   (Or your custom domain, e.g. `https://confirm.yourdomain.com/auth/confirm`.)

2. **Confirm signup email template:**  
   Set the button/link to this **https** URL (so Gmail doesn't strip it):

   ```html
   <a href="https://your-project.pages.dev/auth/confirm?token_hash={{ .TokenHash }}&type=signup"
      style="...">
     Confirm Your PetPulse Account
   </a>
   ```

   Replace `your-project.pages.dev` with your actual Cloudflare Pages URL (or custom domain).

## Flow

1. User taps the link in the email → browser opens `https://.../auth/confirm?token_hash=...&type=signup`.
2. This page redirects to `petpulse://auth/callback?token_hash=...&type=signup`.
3. On the phone, the OS opens the PetPulse app with that URL; the app completes confirmation.
